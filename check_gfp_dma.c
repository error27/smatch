/*
 * sparse/check_gfp_dma.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

/* this is stolen from the kernel but it's totally fair use dude...  */
#define __GFP_DMA       (0x01u)
#define __GFP_HIGHMEM   (0x02u)
#define __GFP_DMA32     (0x04u)
#define __GFP_MOVABLE   (0x08u)
#define GFP_ZONEMASK    (__GFP_DMA|__GFP_HIGHMEM|__GFP_DMA32|__GFP_MOVABLE)

static void match_alloc(const char *fn, struct expression *expr, void *_arg)
{
	int arg_nr = (int)_arg;
	struct expression *arg_expr;
	long long val;

	arg_expr = get_argument_from_call_expr(expr->args, arg_nr);
	if (!get_value(arg_expr, &val))
		return;
	if (val == 0) /* GFP_NOWAIT */
		return;
	if (!(val & ~GFP_ZONEMASK))
		sm_msg("error: no modifiers for allocation.");
}

void check_gfp_dma(int id)
{
	my_id = id;
	if (option_project != PROJ_KERNEL)
		return;
	add_function_hook("kmalloc", &match_alloc, INT_PTR(1));
	add_function_hook("kzalloc", &match_alloc, INT_PTR(1));
}
