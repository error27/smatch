/*
 * smatch/check_pointer_math.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_binop(struct expression *expr)
{
	struct symbol *type;

	if (expr->op != '+')
		return;
	if (expr->right->type != EXPR_SIZEOF)
		return;
	type = get_pointer_type(expr->left);
	if (!type)
		return;
	if (type->bit_size <= 8) /* ignore void, bool and char pointers*/
		return; 
	sm_msg("warn: potential pointer math issue (%d bits)", type->bit_size);

}

void check_pointer_math(int id)
{
	my_id = id;
	add_hook(&match_binop, BINOP_HOOK);
}
