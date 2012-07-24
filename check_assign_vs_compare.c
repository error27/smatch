/*
 * smatch/check_assign_vs_compare.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_condition(struct expression *expr)
{
	long long val;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;

	if (!get_value(expr->right, &val))
		return;
	sm_msg("warn: was '== %lld' instead of '='", val);
}

void check_assign_vs_compare(int id)
{
	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
}
