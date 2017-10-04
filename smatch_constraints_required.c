/*
 * Copyright (C) 2017 Oracle.
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

static int my_id;

static int bytes_per_element(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return 0;

	if (type->type != SYM_PTR && type->type != SYM_ARRAY)
		return 0;

	type = get_base_type(type);
	return type_bytes(type);
}

static void save_constraint_required(struct expression *pointer, int op, struct expression *constraint)
{
	char *data, *limit;

	data = get_constraint_str(pointer);
	if (!data)
		return;

	limit = get_constraint_str(constraint);
	if (!limit) {
		// FIXME deal with <= also
		if (op == '<')
			set_state_expr(my_id, constraint, alloc_state_expr(pointer));
		goto free_data;
	}

	sql_save_constraint_required(data, op, limit);

	free_string(limit);
free_data:
	free_string(data);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *pointer, *call, *arg;
	sval_t sval;

	pointer = strip_expr(expr->left);
	call = strip_expr(expr->right);
	arg = get_argument_from_call_expr(call->args, size_arg);
	arg = strip_expr(arg);

	if (arg->type == EXPR_BINOP && arg->op == '*') {
		struct expression *left, *right;

		left = strip_expr(arg->left);
		right = strip_expr(arg->right);

		if (get_implied_value(left, &sval) &&
		    sval.value == bytes_per_element(pointer))
			arg = right;
		else if (get_implied_value(right, &sval) &&
		    sval.value == bytes_per_element(pointer))
			arg = left;
		else
			return;
	}

	if (arg->type == EXPR_BINOP && arg->op == '+' &&
	    get_implied_value(expr->right, &sval) &&
	    sval.value == 1)
		save_constraint_required(pointer, SPECIAL_LTE, arg->left);
	else
		save_constraint_required(pointer, '<', arg);
}

static void match_calloc(const char *fn, struct expression *expr, void *_start_arg)
{
	struct expression *pointer, *call, *size;
	struct expression *count = NULL;
	int start_arg = PTR_INT(_start_arg);
	sval_t sval;

	pointer = strip_expr(expr->left);
	call = strip_expr(expr->right);

	size = get_argument_from_call_expr(call->args, start_arg);
	if (get_implied_value(size, &sval) &&
	    sval.value == bytes_per_element(pointer))
		count = get_argument_from_call_expr(call->args, start_arg + 1);
	else {
		size = get_argument_from_call_expr(call->args, start_arg + 1);
		if (get_implied_value(size, &sval) &&
		    sval.value == bytes_per_element(pointer))
			count = get_argument_from_call_expr(call->args, start_arg);
	}

	if (!count)
		return;

	save_constraint_required(pointer, '<', count);
}

static void add_allocation_function(const char *func, void *call_back, int param)
{
	add_function_assign_hook(func, call_back, INT_PTR(param));
}

static void match_assign_state(struct expression *expr)
{
	struct smatch_state *state;
	char *data, *limit;

	state = get_state_expr(my_id, expr->right);
	if (!state || !state->data)
		return;

	data = get_constraint_str(state->data);
	if (!data)
		return;

	limit = get_constraint_str(expr->left);
	if (!limit)
		goto free_data;

	sql_save_constraint_required(data, '<', limit);

	free_string(limit);
free_data:
	free_string(data);
}

static void match_assign_ARRAY_SIZE(struct expression *expr)
{
	struct expression *array;
	char *data, *limit;
	const char *macro;

	macro = get_macro_name(expr->right->pos);
	if (!macro || strcmp(macro, "ARRAY_SIZE") != 0)
		return;
	array = strip_expr(expr->right);
	if (array->type != EXPR_BINOP || array->op != '+')
		return;
	array = strip_expr(array->left);
	if (array->type != EXPR_BINOP || array->op != '/')
		return;
	array = strip_expr(array->left);
	if (array->type != EXPR_SIZEOF)
		return;
	array = strip_expr(array->cast_expression);
	if (array->type != EXPR_PREOP || array->op != '*')
		return;
	array = strip_expr(array->unop);

	data = get_constraint_str(array);
	limit = get_constraint_str(expr->left);
	if (!data || !limit)
		goto free;

	sql_save_constraint_required(data, '<', limit);

free:
	free_string(data);
	free_string(limit);
}

void register_constraints_required(int id)
{
	my_id = id;

	add_hook(&match_assign_state, ASSIGNMENT_HOOK);

	add_hook(&match_assign_ARRAY_SIZE, ASSIGNMENT_HOOK);
	add_hook(&match_assign_ARRAY_SIZE, GLOBAL_ASSIGNMENT_HOOK);

	add_allocation_function("malloc", &match_alloc, 0);
	add_allocation_function("memdup", &match_alloc, 1);
	add_allocation_function("realloc", &match_alloc, 1);
	add_allocation_function("realloc", &match_calloc, 0);
	if (option_project == PROJ_KERNEL) {
		add_allocation_function("kmalloc", &match_alloc, 0);
		add_allocation_function("kzalloc", &match_alloc, 0);
		add_allocation_function("vmalloc", &match_alloc, 0);
		add_allocation_function("__vmalloc", &match_alloc, 0);
		add_allocation_function("sock_kmalloc", &match_alloc, 1);
		add_allocation_function("kmemdup", &match_alloc, 1);
		add_allocation_function("kmemdup_user", &match_alloc, 1);
		add_allocation_function("dma_alloc_attrs", &match_alloc, 1);
		add_allocation_function("pci_alloc_consistent", &match_alloc, 1);
		add_allocation_function("pci_alloc_coherent", &match_alloc, 1);
		add_allocation_function("devm_kmalloc", &match_alloc, 1);
		add_allocation_function("devm_kzalloc", &match_alloc, 1);
		add_allocation_function("kcalloc", &match_calloc, 0);
		add_allocation_function("devm_kcalloc", &match_calloc, 1);
		add_allocation_function("krealloc", &match_alloc, 1);
	}
}
