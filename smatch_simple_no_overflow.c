/*
 * Copyright (C) 2015 Oracle.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(no_overflow);

static int in_overflow_test;

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct range_list *rl;

	if (is_capped_var_sym(sm->name, sm->sym))
		return &no_overflow;

	if (!get_user_rl_var_sym(sm->name, sm->sym, &rl))
		return &no_overflow;
	return &undefined;
}

static void match_divide(struct expression *expr)
{
	struct expression *left, *right;
	struct symbol *type;
	sval_t max;

	if (expr->type != EXPR_COMPARE)
		return;
	if (expr->op != '>' && expr->op != SPECIAL_UNSIGNED_GT &&
	    expr->op != SPECIAL_GTE && expr->op != SPECIAL_UNSIGNED_GTE)
		return;

	left = strip_parens(expr->left);
	right = strip_parens(expr->right);

	if (right->type != EXPR_BINOP || right->op != '/')
		return;
	if (!get_value(right->left, &max))
		return;
	if (max.value != INT_MAX && max.value != UINT_MAX &&
	    max.value != LLONG_MAX && max.uvalue != ULLONG_MAX)
		return;

	type = get_type(expr);
	if (!type)
		return;
	if (type_bits(type) != 32 && type_bits(type) != 64)
		return;

	in_overflow_test = 1;
	set_true_false_states_expr(my_id, left, NULL, &no_overflow);
	set_true_false_states_expr(my_id, right->right, NULL, &no_overflow);
}

static int match_overflow_to_less_than_helper(struct expression *left, struct expression *right)
{
	struct symbol *type, *tmp;

	if (left->type != EXPR_BINOP || left->op != '+')
		return 0;

	type = &int_ctype;
	tmp = get_type(left);
	if (type_positive_bits(tmp) > type_positive_bits(type))
		type = tmp;
	tmp = get_type(right);
	if (type_positive_bits(tmp) > type_positive_bits(type))
		type = tmp;

	if (type_bits(type) != 32 && type_bits(type) != 64)
		return 0;

	if (!expr_equiv(left->left, right) && !expr_equiv(left->right, right))
		return 0;

	in_overflow_test = 1;

	set_true_false_states_expr(my_id, left->left, NULL, &no_overflow);
	set_true_false_states_expr(my_id, left->right, NULL, &no_overflow);
	return 1;
}


static void match_overflow_to_less_than(struct expression *expr)
{
	struct expression *left, *right, *tmp;
	int redo = 0;

	if (expr->type != EXPR_COMPARE)
		return;
	if (expr->op != '<' && expr->op != SPECIAL_UNSIGNED_LT &&
	    expr->op != SPECIAL_LTE && expr->op != SPECIAL_UNSIGNED_LTE)
		return;

	left = strip_parens(expr->left);
	right = strip_parens(expr->right);

	if (match_overflow_to_less_than_helper(left, right))
		return;

	tmp = get_assigned_expr(left);
	if (tmp) {
		redo = 1;
		left = tmp;
	}

	tmp = get_assigned_expr(right);
	if (tmp) {
		redo = 1;
		right = tmp;
	}

	if (redo)
		match_overflow_to_less_than_helper(left, right);
}

static void match_do_undo(struct expression *expr)
{
//	struct expression *left, *right;
	struct symbol *type;

	/* if ((x << 12) >> 12 != x) { */

	if (expr->type != EXPR_COMPARE)
		return;
	if (expr->op != SPECIAL_EQUAL && expr->op != SPECIAL_NOTEQUAL)
		return;

	type = get_type(expr);
	if (!type || !type_unsigned(type))
		return;

//	left = strip_parens(expr->left);
//	right = strip_parens(expr->right);
}

static void match_condition(struct expression *expr)
{
	match_overflow_to_less_than(expr);
	match_divide(expr);
	match_do_undo(expr);
}

int expr_overflow_simple(struct expression *expr)
{
	int op;
	sval_t lmax, rmax, res;

	expr = strip_expr(expr);

	if (expr->type == EXPR_ASSIGNMENT) {
		switch(expr->op) {
		case SPECIAL_MUL_ASSIGN:
			op = '*';
			break;
		case SPECIAL_ADD_ASSIGN:
			op = '+';
			break;
		case SPECIAL_SHL_ASSIGN:
			op = SPECIAL_LEFTSHIFT;
			break;
		default:
			return 0;
		}
	} else if (expr->type == EXPR_BINOP) {
		if (expr->op != '*' && expr->op != '+' && expr->op != SPECIAL_LEFTSHIFT)
			return 0;
		op = expr->op;
	} else {
		return 0;
	}

	get_absolute_max(expr->left, &lmax);
	get_absolute_max(expr->right, &rmax);

	if (sval_binop_overflows(lmax, op, rmax))
		return 1;

	res = sval_binop(lmax, op, rmax);
	if (sval_cmp(res, sval_type_max(get_type(expr))) > 0)
		return 1;
	return 0;
}

static void match_assign(struct expression *expr)
{
	if (type_is_ptr(get_type(expr->left)))
		return;

	if (get_state_expr(my_id, expr->right) == &no_overflow)
		set_state_expr(my_id, expr->left, &no_overflow);
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	int i = 0;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;

		if (get_state_expr(my_id, arg) != &no_overflow)
			continue;

		sql_insert_caller_info(expr, NO_OVERFLOW_SIMPLE, i, "$", "");
	} END_FOR_EACH_PTR(arg);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	struct smatch_state *state;
	struct smatch_state *extra_state;

	if (strcmp(sm->state->name, "") == 0)
		return;

	extra_state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (extra_state && type_is_ptr(estate_type(extra_state)))
		return;

	__ignore_param_used++;
	state = get_state(my_id, sm->name, sm->sym);
	__ignore_param_used--;
	if (state != &no_overflow)
		return;
	sql_insert_caller_info(call, NO_OVERFLOW_SIMPLE, param, printed_name, "");
}

static void returned_data(int return_id, char *return_ranges, struct expression *expr)
{
	struct symbol *returned_sym;
	struct stree *start_states;
	struct sm_state *sm;
	const char *param_name;
	int param;

	returned_sym = expr_to_sym(expr);
	start_states = get_start_states();

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		struct smatch_state *start;

		if (sm->state != &no_overflow)
			continue;
		__ignore_param_used++;
		start = get_state_stree(start_states, my_id, sm->name, sm->sym);
		__ignore_param_used--;
		if (start == &no_overflow)
			continue;
		param = get_param_num_from_sym(sm->sym);
		if (param < 0) {
			if (returned_sym != sm->sym)
				continue;
			param = -1;
		}

		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		sql_insert_return_states(return_id, return_ranges,
					 NO_OVERFLOW_SIMPLE, param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

static void db_returns_no_overflow(struct expression *expr, int param, char *key, char *value)
{
	struct expression *call;
	struct expression *arg;
	char *name;
	struct symbol *sym;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (param == -1) {
		name = get_variable_from_key(expr->left, key, &sym);
	} else {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return;
		name = get_variable_from_key(arg, key, &sym);
	}
	if (!name || !sym)
		goto free;

	set_state(my_id, name, sym, &no_overflow);
free:
	free_string(name);
}

static void set_param_no_overflow(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 1);
	else
		return;

	set_state(my_id, fullname, sym, &no_overflow);
}

static void match_stmt(struct statement *stmt)
{
	in_overflow_test = 0;
}

int is_overflow_safe_variable(struct expression *expr)
{
	if (in_overflow_test)
		return 1;
	if (get_state_expr(my_id, expr) == &no_overflow)
		return 1;
	return 0;
}

static void match_safe(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &no_overflow);
}

void register_simple_no_overflow(int id)
{
	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);

	add_hook(&match_stmt, STMT_HOOK);

	add_unmatched_state_hook(my_id, &unmatched_state);

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	add_split_return_callback(&returned_data);
	return_implies_param_key("__builtin_mul_overflow", int_zero, int_zero, &match_safe, 0, "$", NULL);
	return_implies_param_key("__builtin_mul_overflow", int_zero, int_zero, &match_safe, 1, "$", NULL);
	return_implies_param_key("kcalloc", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kcalloc", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);
	return_implies_param_key("kmalloc_array", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kmalloc_array", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);
	return_implies_param_key("kmalloc_array_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kmalloc_array_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);
	return_implies_param_key("kvmalloc_array", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kvmalloc_array", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);
	return_implies_param_key("kvmalloc_array_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kvmalloc_array_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);
	return_implies_param_key("kvmalloc_array_node_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 0, "$", NULL);
	return_implies_param_key("kvmalloc_array_node_noprof", valid_ptr_min_sval, valid_ptr_max_sval, &match_safe, 1, "$", NULL);

	select_return_states_hook(NO_OVERFLOW_SIMPLE, &db_returns_no_overflow);
	select_caller_info_hook(set_param_no_overflow, NO_OVERFLOW_SIMPLE);
}

