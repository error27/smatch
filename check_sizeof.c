/*
 * smatch/check_sizeof.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void check_pointer(struct expression *expr, char *ptr_name)
{
	char *name;
	sval_t sval;

	if (!expr || expr->type != EXPR_SIZEOF)
		return;

	get_value(expr, &sval);

	expr = strip_expr(expr->cast_expression);
	name = expr_to_str(expr);
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

	ptr_name = expr_to_str(expr->left);
	if (!ptr_name)
		return;

	FOR_EACH_PTR(call->args, arg) {
		check_pointer(arg, ptr_name);
	} END_FOR_EACH_PTR(arg);

	free_string(ptr_name);
}

static void check_passes_pointer(char *name, struct expression *call)
{
	struct expression *arg;
	char *ptr_name;

	FOR_EACH_PTR(call->args, arg) {
		ptr_name = expr_to_var(arg);
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
		name = expr_to_var(obj);
		if (!name)
			continue;
		check_passes_pointer(name, call);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

static void match_sizeof(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&')
		sm_msg("warn: sizoef(&pointer)?");
	if (expr->type == EXPR_SIZEOF)
		sm_msg("warn: sizoef(sizeof())?");
	/* the ilog2() macro is a valid place to check the size of a binop */
	if (expr->type == EXPR_BINOP && !get_macro_name(expr->pos))
		sm_msg("warn: taking sizeof binop");
}

void check_sizeof(int id)
{
	my_id = id;

	add_hook(&match_call_assignment, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_check_params, FUNCTION_CALL_HOOK);
	add_hook(&match_sizeof, SIZEOF_HOOK);
}
