/*
 * smatch/check_sizeof_pointer.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void check_sizeof(struct expression *expr, char *ptr_name)
{
	char *name;
	long long val;

	if (!expr || expr->type != EXPR_SIZEOF)
		return;

	get_value(expr, &val);

	expr = strip_expr(expr->cast_expression);
	name = get_variable_from_expr_complex(expr, NULL);
	if (!name)
		return;

	if (strcmp(ptr_name, name) == 0)
		sm_msg("warn: was 'sizeof(*%s)' intended?", ptr_name);
	free_string(name);
}

static void match_call_assignment(struct expression *expr)
{
	struct expression *call = strip_expr(expr->right);
	struct expression *arg;
	char *ptr_name;

	if (!is_pointer(expr->left))
		return;

	ptr_name = get_variable_from_expr_complex(expr->left, NULL);
	if (!ptr_name)
		return;

	FOR_EACH_PTR(call->args, arg) {
		check_sizeof(arg, ptr_name);
	} END_FOR_EACH_PTR(arg);
}

static void check_passes_pointer(char *name, struct expression *call)
{
	struct expression *arg;
	char *ptr_name;

	FOR_EACH_PTR(call->args, arg) {
		ptr_name = get_variable_from_expr(arg, NULL);
		if (!ptr_name)
			continue;
		if (strcmp(name, ptr_name) == 0)
			sm_msg("warn: was 'sizeof(*%s)' intended?", name);
		free_string(ptr_name);
	} END_FOR_EACH_PTR(arg);
}

static void match_check_params(struct expression *call)
{
	struct expression *arg;
	struct expression *obj;
	char *name;

	FOR_EACH_PTR(call->args, arg) {
		if (arg->type != EXPR_SIZEOF)
			continue;
		obj = strip_expr(arg->cast_expression);
		if (!is_pointer(obj))
			continue;
		name = get_variable_from_expr(obj, NULL);
		if (!name)
			continue;
		check_passes_pointer(name, call);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

void check_sizeof_pointer(int id)
{
	my_id = id;

	add_hook(&match_call_assignment, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_check_params, FUNCTION_CALL_HOOK);
}
