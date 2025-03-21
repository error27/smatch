/*
 * Copyright (C) 2013 Oracle.
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

static bool is_select_assign(struct expression *expr)
{
	/* select assignments are faked in smatch_conditions.c */
	expr = expr_get_parent_expr(expr);
	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return false;
	expr = expr_get_parent_expr(expr);
	if (!expr)
		return false;
	if (expr->type == EXPR_CONDITIONAL ||
	    expr->type == EXPR_SELECT)
		return true;
	return false;
}

static int is_comparison_call(struct expression *expr)
{
	expr = expr_get_parent_expr(expr);
	if (!expr || expr->type != EXPR_COMPARE)
		return 0;
	if (expr->op != SPECIAL_EQUAL && expr->op != SPECIAL_NOTEQUAL)
		return 0;
	return 1;
}

static bool is_switch_condition(struct expression *expr)
{
	struct statement *stmt;

	stmt = expr_get_parent_stmt(expr);
	if (stmt && stmt->type == STMT_SWITCH)
		return true;
	return false;
}

static bool is_condition_expr(struct expression *expr)
{
	if (is_comparison_call(expr) ||
	    is_select_assign(expr) ||
	    is_switch_condition(expr))
		return true;
	return false;
}

static int next_line_is_if(struct expression *expr)
{
	struct expression *next;

	if (!__next_stmt || __next_stmt->type != STMT_IF)
		return 0;

	next = strip_expr(__next_stmt->if_conditional);
	while (next->type == EXPR_PREOP && next->op == '!')
		next = strip_expr(next->unop);
	if (expr_equiv(expr, next))
		return 1;
	return 0;
}

static int next_line_checks_IS_ERR(struct expression *call, struct expression *arg)
{
	struct expression *next;
	struct expression *tmp;

	tmp = expr_get_parent_expr(call);
	if (tmp && tmp->type == EXPR_ASSIGNMENT) {
		if (next_line_checks_IS_ERR(NULL, tmp->left))
			return 1;
	}

	if (!__next_stmt || __next_stmt->type != STMT_IF)
		return 0;

	next = strip_expr(__next_stmt->if_conditional);
	while (next->type == EXPR_PREOP && next->op == '!')
		next = strip_expr(next->unop);
	if (!next || next->type != EXPR_CALL)
		return 0;
	if (next->fn->type != EXPR_SYMBOL || !next->fn->symbol ||
	    !next->fn->symbol->ident ||
	    (strcmp(next->fn->symbol->ident->name, "IS_ERR") != 0 &&
	     strcmp(next->fn->symbol->ident->name, "IS_ERR_OR_NULL") != 0))
		return 0;
	next = get_argument_from_call_expr(next->args, 0);
	return expr_equiv(next, arg);
}

static int is_non_zero_int(struct range_list *rl)
{
	struct data_range *tmp;
	int cnt = -1;

	FOR_EACH_PTR(rl, tmp) {
		cnt++;

		if (cnt == 0) {
			if (tmp->min.value == INT_MIN &&
			    tmp->max.value == -1)
				continue;
		} else if (cnt == 1) {
			if (tmp->min.value == 1 &&
			    tmp->max.value == INT_MAX)
				return 1;
		}
		return 0;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static int is_valid_ptr(sval_t sval)
{
	if (sval.value == INT_MIN || sval.value == INT_MAX)
		return 0;

	if (sval_cmp(valid_ptr_min_sval, sval) <= 0 &&
	    sval_cmp(valid_ptr_max_sval, sval) >= 0) {
		return 1;
	}
	return 0;
}

static int has_distinct_zero(struct range_list *rl)
{
	struct data_range *tmp;

	FOR_EACH_PTR(rl, tmp) {
		if (tmp->min.value == 0 || tmp->max.value == 0)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static bool has_distinct_positive(struct range_list *rl)
{
	sval_t max;
	long long max_value;

	/*
	 * Initializially, I imagined only looking at the last range in
	 * the range list.  Return true if it points to a single value
	 * and then it's greater than zero.  But actually that doesn't
	 * totally work because one bug was range 4-5.  And also there
	 * is no need to make it so complicated.
	 *
	 */

	if (!rl)
		return false;

	max = rl_max(rl);
	/* extent the sign bit when compiling with -m32 */
	max_value = max.value;
	if (type_is_ptr(rl_type(rl)) &&
	    type_bits(rl_type(rl)) == 32 &&
	    sizeof(void *) == 8)
		max_value = (int)max_value;
	if (max_value > 0 && !sval_is_a_max(max))
		return true;
	return false;
}

static void match_err_ptr(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg_expr;
	struct sm_state *sm, *tmp;
	int arg = PTR_INT(data);

	if (is_impossible_path())
		return;

	arg_expr = get_argument_from_call_expr(expr->args, arg);
	sm = get_sm_state_expr(SMATCH_EXTRA, arg_expr);
	if (!sm)
		return;

	if (is_condition_expr(expr))
		return;

	if (next_line_checks_IS_ERR(expr, arg_expr))
		return;
	if (strcmp(fn, "ERR_PTR") == 0 &&
	    next_line_is_if(arg_expr))
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		sval_t sval;

		if (!estate_rl(tmp->state))
			continue;
		if (estate_type(tmp->state) == &llong_ctype)
			continue;
		if (is_non_zero_int(estate_rl(tmp->state)))
			continue;
		if (has_distinct_zero(estate_rl(tmp->state))) {
			sm_warning("passing zero to '%s'", fn);
			return;
		}

		if (has_distinct_positive(estate_rl(tmp->state))) {
			sm_warning("passing positive error code '%s' to '%s'", tmp->state->name, fn);
			return;
		}

		if (estate_get_single_value(tmp->state, &sval) && sval.value < -4096) {
			sm_warning("passing invalid error code %lld to '%s'", sval.value, fn);
			return;
		}

		if (strcmp(fn, "PTR_ERR") != 0)
			continue;
		if (is_valid_ptr(estate_min(tmp->state)) &&
		    is_valid_ptr(estate_max(tmp->state))) {
			sm_warning("passing a valid pointer to '%s'", fn);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

void check_zero_to_err_ptr(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_function_hook("ERR_PTR", &match_err_ptr, INT_PTR(0));
	add_function_hook("ERR_CAST", &match_err_ptr, INT_PTR(0));
	add_function_hook("PTR_ERR", &match_err_ptr, INT_PTR(0));
	add_function_hook("dev_err_probe", &match_err_ptr, INT_PTR(1));
}
