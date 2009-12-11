/*
 * sparse/check_precedence.c
 *
 * Copyright (C) 20XX Your Name.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_condition(struct expression *expr)
{
	if (expr->type != EXPR_BINOP)
		return;
	if (expr->left->type == EXPR_COMPARE || 
		expr->right->type == EXPR_COMPARE)
		sm_msg("warning: do you want parens here?");
}

void check_precedence(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
}
