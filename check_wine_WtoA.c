/*
 * sparse/check_template.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Idea from Michael Stefaniuc and Vincent Béron's earlier WtoA
 * check.
 *
 * Apparently when you are coding WINE, you are not allowed to call 
 * functions that end in capital 'A' from functions that end in 
 * capital 'W'
 *
 */

#include "smatch.h"

static int my_id;

static int in_w = 0;

static void match_function_def(struct symbol *sym)
{
	char *func = get_function();
	int len;

	if (!func) {
		in_w = 0;
		return;
	}
	len = strlen(func);
	if (func[len - 1] == 'W' && len > 2 && func[len - 2] != 'A' )
		in_w = 1;
	else
		in_w = 0;
}

static int allowed_func(const char *fn)
{
	if (!strcmp("lstrcatA", fn))
		return 1;
	if (!strcmp("lstrcpyA", fn))
		return 1;
	if (!strcmp("lstrcpynA", fn))
		return 1;
	if (!strcmp("lstrlenA", fn))
		return 1;
	return 0;
}

static void match_call(struct expression *expr)
{
	char *fn_name;
	int len;

	if (!in_w)
		return;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name)
		goto free;
	len = strlen(fn_name);
	if (fn_name[len - 1] == 'A' && !allowed_func(fn_name)) {
		sm_msg("warn: WtoA call %s()", fn_name);
	}
free:
	free_string(fn_name);
}

void check_wine_WtoA(int id)
{
	if (option_project != PROJ_WINE)
		return;

	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
