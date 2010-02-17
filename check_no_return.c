/*
 * smatch/check_no_return.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;
static int returned;

static void match_return(struct expression *ret_value)
{
	if (is_reachable())
		returned = 1;
}

static int function_is_static()
{
	if (cur_func_sym->ctype.modifiers & MOD_STATIC)
		return 1;
	return 0;
}

static void match_func_end(struct symbol *sym)
{
	if (!function_is_static() && !is_reachable() && !returned)
		sm_info("info: add to no_return_funcs");
	returned = 0;
}

void check_no_return(int id)
{
	if (!option_info)
		return;
	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);
}
