/*
 * sparse/check_logical_instead_of_bitwise.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static int is_bitshift(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type != EXPR_BINOP)
		return 0;
	if (expr->op == SPECIAL_LEFTSHIFT || expr->op == SPECIAL_RIGHTSHIFT)
		return 1;
	return 0;
}

static void match_logic(struct expression *expr)
{
	long long val;

	if (expr->type != EXPR_LOGICAL)
		return;

	if (get_macro_name(expr->pos))
		return;

	if (!get_value(expr->right, &val)) {
		if (!get_value(expr->left, &val))
			return;
	}

	if (val == 0 || val == 1)
		return;

	sm_msg("warn: should this be a bitwise op?");
}

static void match_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->type != EXPR_LOGICAL)
		return;
	if (is_bitshift(right->left) || is_bitshift(right->right))
		sm_msg("warn: should this be a bitwise op?");
}

void check_logical_instead_of_bitwise(int id)
{
	my_id = id;

	add_hook(&match_logic, LOGIC_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
