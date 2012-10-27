/*
 * sparse/check_bogus_irqrestore.c
 *
 * Copyright (C) 2011 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_irqrestore(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg_expr;
	sval_t tmp;

	arg_expr = get_argument_from_call_expr(expr->args, 1);
	if (!get_implied_value_sval(arg_expr, &tmp))
		return;
	sm_msg("error: calling '%s()' with bogus flags", fn);
}

void check_bogus_irqrestore(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_function_hook("spin_unlock_irqrestore", &match_irqrestore, NULL);
}
