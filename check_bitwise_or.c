/*
 * sparse/check_bitwise_or.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * I can't think of a place where it makes sense to use a bitwise
 * or in a condition.  Either people want bitwise and or regular or.
 */

#include "smatch.h"

static int my_id;

static void match_condition(struct expression *expr)
{
	expr = strip_expr(expr);
	if (expr->type != EXPR_BINOP)
		return;
	if (expr->op == '|')
		sm_msg("error: bitwise or used in condition.");
}

void check_bitwise_or(int id)
{
	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
}
