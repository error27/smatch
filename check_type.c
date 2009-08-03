/*
 * sparse/check_type.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_free(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg_expr;
	char *name;
	struct symbol *sym;
	struct symbol *type;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr(arg_expr, &sym);
	if (!name ||!sym)
		goto exit;
	type = get_ptr_type(arg_expr);
	if (type && type->ident && !strcmp("sk_buff", type->ident->name))
		smatch_msg("error: use kfree_skb() here instead of kfree()");
exit:
	free_string(name);
}

void check_type(int id)
{
	if (!option_spammy)
		return;

	my_id = id;
	add_function_hook("kfree", &match_free, NULL);
}
