/*
 * smatch/check_kmalloc_wrong_size.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static int get_data_size(struct expression *ptr)
{
	struct symbol *type;
	int ret;

	type = get_type(ptr);
	if (!type || type->type != SYM_PTR)
		return 0;
	type = get_base_type(type);
	if (!type)
		return 0;
	ret = bits_to_bytes(type->bit_size);
	if (ret == -1)
		return 0;
	return ret;
}

static void check_size_matches(int data_size, struct expression *size_expr)
{
	long long val;

	if (data_size == 1)  /* this is generic a buffer */
		return;

	if (!get_implied_value(size_expr, &val))
		return;
	if (val != data_size)
		sm_msg("warn: double check that we're allocating correct size: %d vs %lld", data_size, val);
}

static void match_alloc(const char *fn, struct expression *expr, void *unused)
{
	struct expression *call = strip_expr(expr->right);
	struct expression *arg;
	int ptr_size;

	ptr_size = get_data_size(expr->left);
	if (!ptr_size)
		return;

	arg = get_argument_from_call_expr(call->args, 0);
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_BINOP || arg->op != '*')
		return;
	if (expr->left->type == EXPR_SIZEOF)
		check_size_matches(ptr_size, arg->left);
	if (expr->right->type == EXPR_SIZEOF)
		check_size_matches(ptr_size, arg->right);
}

static void match_calloc(const char *fn, struct expression *expr, void *_arg_nr)
{
	int arg_nr = PTR_INT(_arg_nr);
	struct expression *call = strip_expr(expr->right);
	struct expression *arg;
	int ptr_size;

	ptr_size = get_data_size(expr->left);
	if (!ptr_size)
		return;

	arg = get_argument_from_call_expr(call->args, arg_nr);
	check_size_matches(ptr_size, arg);
}

void check_kmalloc_wrong_size(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL) {
		add_function_assign_hook("malloc", &match_alloc, NULL);
		add_function_assign_hook("calloc", &match_calloc, INT_PTR(1));
		return;
	}

	add_function_assign_hook("kmalloc", &match_alloc, NULL);
	add_function_assign_hook("kcalloc", &match_calloc, INT_PTR(1));
}
