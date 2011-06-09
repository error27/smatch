/*
 * sparse/check_memset.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_memset(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg_expr;
	long long val;

	arg_expr = get_argument_from_call_expr(expr->args, 2);

	if (arg_expr->type != EXPR_VALUE)
		return;
	if (!get_value(arg_expr, &val))
		return;
	if (val != 0)
		return;
	sm_msg("error: calling memset(x, y, 0);");
}

void check_memset(int id)
{
	my_id = id;
	add_function_hook("memset", &match_memset, NULL);
	add_function_hook("__builtin_memset", &match_memset, NULL);
}
