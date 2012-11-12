/*
 * smatch/check_mod_timer.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static void match_mod_timer(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	sval_t sval;

	arg = get_argument_from_call_expr(expr->args, 1);
	if (!get_value(arg, &sval) || sval.value == 0)
		return;
	sm_msg("warn: mod_timer() takes an absolute time not an offset.");
}

void check_mod_timer(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_function_hook("mod_timer", &match_mod_timer, NULL);
}
