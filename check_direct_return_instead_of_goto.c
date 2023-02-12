/*
 * Copyright (C) 2022 Oracle.
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

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static struct statement *goto_stmt, *next_goto;
static struct expression *return_expr;

static unsigned long set_label;
static unsigned long label_cnt;

static void reset(void)
{
	goto_stmt = NULL;
	next_goto = NULL;
	return_expr = NULL;
}

static bool is_do_nothing_goto(struct statement *goto_stmt)
{
	struct symbol *fn;
	struct statement *stmt;

	fn = get_base_type(cur_func_sym);
	if (!fn)
		return false;
	stmt = fn->stmt;
	if (!stmt)
		stmt = fn->inline_stmt;
	if (!stmt || stmt->type != STMT_COMPOUND)
		return false;
	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (!stmt)
		return false;
	if (stmt->type != STMT_LABEL)
		return false;

	if (!stmt->label_identifier ||
	    stmt->label_identifier->type != SYM_LABEL ||
	    !stmt->label_identifier->ident)
		return false;

	if (strcmp(stmt->label_identifier->ident->name,
		   goto_stmt->goto_label->ident->name) == 0)
		return true;

	return false;
}

static bool is_printk_stmt(struct statement *stmt)
{
	char *str;

	if (!stmt)
		return false;

	str = pos_ident(stmt->pos);
	if (!str)
		return false;

	if (strcmp(str, "dev_err") == 0 ||
	    strcmp(str, "dev_info") == 0 ||
	    strcmp(str, "dev_warn") == 0 ||
	    strcmp(str, "dev_notice") == 0 ||
	    strcmp(str, "dev_dbg") == 0)
		return true;

	if (strcmp(str, "pr_err") == 0 ||
	    strcmp(str, "pr_info") == 0 ||
	    strcmp(str, "pr_warn") == 0 ||
	    strcmp(str, "pr_notice") == 0 ||
	    strcmp(str, "pr_debug") == 0)
		return true;

	if (strstr(str, "_dev_err") ||
	    strstr(str, "_dev_warn") ||
	    strstr(str, "_dev_dbg"))
		return true;

	return false;
}

static bool label_name_matches(struct statement *goto_stmt,
			       struct statement *stmt)
{
	if (!stmt)
		return false;

	if (stmt->type != STMT_LABEL)
		return false;

	if (!stmt->label_identifier ||
	    stmt->label_identifier->type != SYM_LABEL ||
	    !stmt->label_identifier->ident)
		return false;

	if (strcmp(stmt->label_identifier->ident->name,
		   goto_stmt->goto_label->ident->name) == 0)
		return true;

	return false;
}

static bool is_printk_goto(struct statement *goto_stmt)
{
	struct symbol *fn;
	struct statement *stmt, *tmp;

	fn = get_base_type(cur_func_sym);
	if (!fn)
		return false;
	stmt = fn->stmt;
	if (!stmt)
		stmt = fn->inline_stmt;
	if (!stmt || stmt->type != STMT_COMPOUND)
		return false;

	FOR_EACH_PTR_REVERSE(stmt->stmts, tmp) {
		if (tmp->type == STMT_RETURN)
			continue;
		if (tmp->type == STMT_LABEL &&
		    !is_printk_stmt(tmp->label_statement))
			return false;
		if (is_printk_stmt(tmp))
			continue;
		if (label_name_matches(goto_stmt, tmp))
			return true;
		return false;
	} END_FOR_EACH_PTR_REVERSE(tmp);

	return false;
}

static void match_goto(struct statement *stmt)
{
	/* Find the first goto */

	if (next_goto)
		return;

	if (stmt->type != STMT_GOTO ||
	    !stmt->goto_label ||
	    stmt->goto_label->type != SYM_LABEL ||
	    !stmt->goto_label->ident)
		return;

	if (is_do_nothing_goto(stmt))
		return;

	if (is_printk_goto(stmt))
		return;

	goto_stmt = stmt;
}

static void match_return(struct expression *expr)
{
	struct range_list *rl;

	if (next_goto)
		return;
	if (!goto_stmt)
		return;

	if (!expr ||
	    !get_implied_rl(expr, &rl) ||
	    success_fail_return(rl) != RET_FAIL) {
		reset();
		return;
	}

	return_expr = expr;
}

static void match_next_goto(struct statement *stmt)
{
	if (next_goto)
		return;

	if (stmt->type != STMT_GOTO ||
	    !stmt->goto_label ||
	    stmt->goto_label->type != SYM_LABEL ||
	    !stmt->goto_label->ident)
		return;

	if (!goto_stmt || !return_expr) {
		reset();
		return;
	}

	next_goto = stmt;
}

static void add_label_cnt(struct statement *stmt)
{
	if (!next_goto) {
		reset();
		return;
	}

	if (!stmt ||
	    !stmt->label_identifier ||
	    stmt->label_identifier->type != SYM_LABEL ||
	    !stmt->label_identifier->ident)
		return;

	if (strcmp(stmt->label_identifier->ident->name,
		   goto_stmt->goto_label->ident->name) == 0)
		label_cnt++;

	if (strcmp(stmt->label_identifier->ident->name,
		   next_goto->goto_label->ident->name) == 0)
		label_cnt++;
}

static void match_label(struct statement *stmt)
{
	sval_t sval;

	if (stmt->type != STMT_LABEL)
		return;

	if (get_state(my_id, "cleanup", NULL) == &true_state) {
		/* The second label in a cleanup block is still cleanup. */
		set_label = true;
		add_label_cnt(stmt);
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
	add_label_cnt(stmt);
}

static void match_label_after(struct statement *stmt)
{
	if (stmt->type != STMT_LABEL)
		return;
	if (set_label) {
		set_state(my_id, "cleanup", NULL, &true_state);
		set_label = false;
	}
}

static void match_final_return(struct expression *expr)
{
	struct range_list *rl;

	if (!expr)
		return;
	if (!next_goto)
		return;
	if (label_cnt != 2)
		return;

	if (get_state(my_id, "cleanup", NULL) != &true_state)
		return;

	if (!get_implied_rl(expr, &rl))
		return;
	if (success_fail_return(rl) != RET_FAIL)
		return;

	sm_warning_line(return_expr->pos.line, "missing unwind goto?");
}

void check_direct_return_instead_of_goto(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&set_label);
	add_function_data(&label_cnt);

	add_function_data((unsigned long *)&goto_stmt);
	add_function_data((unsigned long *)&return_expr);
	add_function_data((unsigned long *)&next_goto);

	// check them in reverse order
	add_hook(&match_next_goto, STMT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_goto, STMT_HOOK);
	add_hook(&match_final_return, RETURN_HOOK);


	add_hook(&match_label, STMT_HOOK);
	add_hook(&match_label_after, STMT_HOOK_AFTER);

}
