/*
 * Copyright (C) 2020 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

/*
 * This check looks for code like:
 *
 *	return 0;
 *
 * err_foo:
 *	free(foo);
 * err_bar:
 *	free(bar);
 *
 *	return ret;
 *
 * And the bug is that some error paths forgot to set "ret" to an error code.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

/*
 * Instead of having a bunch of different states declared here,
 * what this does is it has "goto == &yup" and "cleanup == &yup"
 *
 */
STATE(yup);

static bool set_label;

static void match_goto(struct statement *stmt)
{
	if (stmt->type != STMT_GOTO)
		return;
	set_state(my_id, "goto", NULL, &yup);
}

static void match_label(struct statement *stmt)
{
	sval_t sval;

	if (stmt->type != STMT_LABEL)
		return;
	if (get_state(my_id, "cleanup", NULL) == &yup) {
		/* The second label in a cleanup block is still cleanup. */
		set_label = true;
		return;
	}

	/*
	 * If the cleanup block isn't preceded by a "return 0;" then it could
	 * be a success path.
	 *
	 */
	if (__get_cur_stree())
		return;
	if (!__prev_stmt || __prev_stmt->type != STMT_RETURN)
		return;
	if (!get_implied_value(__prev_stmt->ret_value, &sval) || sval.value != 0)
		return;
	/* We can't set the state until after we've merged the gotos. */
	set_label = true;
}

static void match_label_after(struct statement *stmt)
{
	if (stmt->type != STMT_LABEL)
		return;
	if (set_label) {
		set_state(my_id, "cleanup", NULL, &yup);
		set_label = false;
	}
}

static bool find_pool(struct sm_state *extra_sm, struct stree *pool)
{
	if (!extra_sm || !pool)
		return false;

	if (extra_sm->pool == pool)
		return true;
	if (find_pool(extra_sm->left, pool))
		return true;
	if (find_pool(extra_sm->right, pool))
		return true;

	return false;
}

static int check_pool(struct sm_state *goto_sm, struct sm_state *extra_sm)
{
	struct sm_state *old;
	struct stree *orig;
	int line = 0;
	sval_t sval;

	if (!goto_sm->pool)
		return 0;

	orig = __swap_cur_stree(goto_sm->pool);

	old = get_sm_state(SMATCH_EXTRA, extra_sm->name, extra_sm->sym);
	if (!old)
		goto swap;
	if (goto_sm->line < old->line)
		goto swap;  // hopefully this is impossible
	if (goto_sm->line < old->line + 3)
		goto swap;  // the ret value is deliberately set to zero
	if (!estate_get_single_value(old->state, &sval) || sval.value != 0)
		goto swap;  // the ret variable is not zero
	if (!find_pool(extra_sm, old->pool))
		goto swap;  // the ret variable was modified after the goto

	line = goto_sm->line;
swap:
	__swap_cur_stree(orig);
	return line;
}

static int recursive_search_for_zero(struct sm_state *goto_sm, struct sm_state *extra_sm)
{
	int line;

	if (!goto_sm)
		return 0;

	line = check_pool(goto_sm, extra_sm);
	if (line)
		return line;

	line = recursive_search_for_zero(goto_sm->left, extra_sm);
	if (line)
		return line;
	line = recursive_search_for_zero(goto_sm->right, extra_sm);
	if (line)
		return line;

	return 0;
}

static void match_return(struct statement *stmt)
{
	struct sm_state *goto_sm, *extra_sm;
	char *name;
	int line;

	if (stmt->type != STMT_RETURN)
		return;
	if (!stmt->ret_value || stmt->ret_value->type != EXPR_SYMBOL)
		return;
	if (cur_func_return_type() != &int_ctype)
		return;
	if (get_state(my_id, "cleanup", NULL) != &yup)
		return;
	goto_sm = get_sm_state(my_id, "goto", NULL);
	if (!goto_sm || goto_sm->state != &yup)
		return;
	extra_sm = get_extra_sm_state(stmt->ret_value);
	if (!extra_sm)
		return;
	if (!estate_rl(extra_sm->state) ||
	    !sval_is_negative(rl_min(estate_rl(extra_sm->state))))
		return;
	line = recursive_search_for_zero(goto_sm, extra_sm);
	if (!line)
		return;

	name = expr_to_str(stmt->ret_value);
	sm_printf("%s:%d %s() warn: missing error code '%s'\n",
		  get_filename(), line, get_function(), name);
	free_string(name);
}

void check_missing_error_code(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_hook(&match_goto, STMT_HOOK);
	add_hook(&match_label, STMT_HOOK);
	add_hook(&match_label_after, STMT_HOOK_AFTER);
	add_hook(&match_return, STMT_HOOK);
}
