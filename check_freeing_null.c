/*
 * sparse/check_freeing_null.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_free(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg_expr;
	char *name;
	long long val;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	if (!get_implied_value(arg_expr, &val))
		return;
	if (val != 0)
		return;
	name = get_variable_from_expr(arg_expr, NULL);
	sm_msg("warn: calling %s() when '%s' is always NULL.", fn, name);
	free_string(name);
}

void check_freeing_null(int id)
{
	my_id = id;
	if (!option_spammy)
		return;
	if (option_project == PROJ_KERNEL)
		add_function_hook("kfree", &match_free, NULL);
	else
		add_function_hook("free", &match_free, NULL);
}
