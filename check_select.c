/*
 * smatch/check_select.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static void match_select(struct expression *expr)
{
	if (expr->cond_true)
		return;
	expr = strip_expr(expr->conditional);
	if (expr->type != EXPR_COMPARE)
		return;
	sm_msg("warn: boolean comparison inside select");
}

void check_select(int id)
{
	my_id = id;
	add_hook(&match_select, SELECT_HOOK);
}
