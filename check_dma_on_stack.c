/*
 * sparse/check_dma_on_stack.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

struct {
	const char *name;
	int param;
} dma_funcs[] = {
	{"usb_control_msg", 6},
};

static void match_dma_func(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	struct symbol *sym;
	char *name;

	arg = get_argument_from_call_expr(expr->args, (int)param);
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_SYMBOL)
		return;
	sym = get_type(arg);
	if (!sym || sym->type != SYM_ARRAY)
		return;
	name = get_variable_from_expr(arg, NULL);
	sm_msg("error: doing dma on the stack (%s)", name);
	free_string(name);
}

void check_dma_on_stack(int id)
{
	int i;

	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	for (i = 0; i < ARRAY_SIZE(dma_funcs); i++) {
		add_function_hook(dma_funcs[i].name, &match_dma_func, 
				(void *)dma_funcs[i].param);
	}
}
