/*
 * Copyright (C) 2010 Dan Carpenter.
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
 * check_memory() is getting too big and messy.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(freed);
STATE(ok);

static void ok_to_use(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static int is_freed(struct expression *expr)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &freed))
		return 1;
	return 0;
}

static void match_symbol(struct expression *expr)
{
	char *name;

	if (!is_freed(expr))
		return;
	name = expr_to_var(expr);
	sm_msg("warn: '%s' was already freed.", name);
	free_string(name);
}

static void match_dereferences(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;
	expr = strip_expr(expr->unop);

	if (!is_freed(expr))
		return;
	name = expr_to_var_sym(expr, NULL);
	sm_msg("error: dereferencing freed memory '%s'", name);
	set_state_expr(my_id, expr, &ok);
	free_string(name);
}

static void match_free(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(param));
	if (!arg)
		return;
	/* option_spammy already prints a warning here */
	if (!option_spammy && is_freed(arg)) {
		char *name = expr_to_var_sym(arg, NULL);

		sm_msg("error: double free of '%s'", name);
		free_string(name);
	}
	set_state_expr(my_id, arg, &freed);
}

static void set_param_freed(struct expression *arg, char *unused)
{
	set_state_expr(my_id, arg, &freed);
}

void check_free(int id)
{
	my_id = id;

	if (option_project == PROJ_KERNEL) {
		add_function_hook("kfree", &match_free, INT_PTR(0));
		add_function_hook("kmem_cache_free", &match_free, INT_PTR(1));
	} else {
		add_function_hook("free", &match_free, INT_PTR(0));
	}

	if (option_spammy)
		add_hook(&match_symbol, SYM_HOOK);
	else
		add_hook(&match_dereferences, DEREF_HOOK);

	add_modification_hook(my_id, &ok_to_use);
	select_call_implies_hook(PARAM_FREED, &set_param_freed);
}
