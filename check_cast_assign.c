/*
 * smatch/check_cast_assign.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static struct symbol *get_cast_type(struct expression *expr)
{
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return NULL;
	expr = strip_parens(expr->unop);
	if (expr->type != EXPR_CAST)
		return NULL;
	return get_pointer_type(expr);
}

static void match_overflow(struct expression *expr)
{
	struct expression *ptr;
	struct symbol *type;
	int cast_size;
	int data_size;

	type = get_cast_type(expr->left);
	if (!type)
		return;
	cast_size = bits_to_bytes(type->bit_size);

	ptr = strip_expr(expr->left->unop);
	data_size = get_array_size_bytes_min(ptr);
	if (data_size <= 0)
		return;
	if (data_size >= cast_size)
		return;
	sm_msg("warn: potential memory corrupting cast %d vs %d bytes",
	       cast_size, data_size);
}

void check_cast_assign(int id)
{
	my_id = id;
	add_hook(&match_overflow, ASSIGNMENT_HOOK);
}

