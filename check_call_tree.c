/*
 * sparse/check_call_tree.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_call(struct expression *expr)
{
	char *fn_name;

	fn_name = expr_to_var(expr->fn);
	if (!fn_name)
		return;
	sm_prefix();
	sm_printf("info: func_call (");
	print_held_locks();
	sm_printf(") %s\n", fn_name);
	free_string(fn_name);
}

void check_call_tree(int id)
{
	if (!option_call_tree)
		return;
	my_id = id;
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
