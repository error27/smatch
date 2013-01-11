/*
 * smatch/check_wait_for_common.c
 *
 * Copyright (C) 2011 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_wait_for_common(const char *fn, struct expression *expr, void *unused)
{
	char *name;

	if (!expr_unsigned(expr->left))
		return;
	name = expr_to_str_sym_complex(expr->left, NULL);
	sm_msg("error: '%s()' returns negative and '%s' is unsigned", fn, name);
	free_string(name);
}

void check_wait_for_common(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;
	add_function_assign_hook("wait_for_common", &match_wait_for_common, NULL);
	add_function_assign_hook("wait_for_completion_interruptible_timeout", &match_wait_for_common, NULL);
}
