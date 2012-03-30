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

void check_logical_instead_of_bitwise(int id)
{
	my_id = id;

	add_hook(&match_logic, LOGIC_HOOK);
}
