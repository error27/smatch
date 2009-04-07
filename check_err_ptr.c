/*
 * sparse/check_err_ptr.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/* 
 * Functions should never return both NULL and ERR_PTR().
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static struct symbol *this_func;
static int err_ptr = 0;
static int returns_null = 0;

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

static void match_err_ptr(const char *fn, struct expression *expr, void *info)
{
	if (!err_ptr)
		smatch_msg("info:  returns_err_ptr");
	err_ptr = 1;
}

static void match_return(struct statement *stmt)
{
	if (expr_to_val(stmt->ret_value) != 0)
		return;
	if (!returns_null)
		smatch_msg("info:  returns_null");
	returns_null = 1;
}

static void match_end_func(struct symbol *sym)
{
	err_ptr = 0;
	returns_null = 0;
}

void check_err_ptr(int id)
{
	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_function_hook("ERR_PTR", &match_err_ptr, NULL);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
