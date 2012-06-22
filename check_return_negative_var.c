/*
 * smatch/check_return.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static void match_return(struct expression *ret_value)
{
	struct expression *expr;
	char *macro;

	if (!ret_value)
		return;
	expr = ret_value;
	if (ret_value->type != EXPR_PREOP || ret_value->op != '-')
		return;

	macro = get_macro_name(expr->unop->pos);
	if (macro && !strcmp(macro, "PTR_ERR")) {
		sm_msg("warn: returning -%s()", macro);
		return;
	}

	if (!option_spammy)
		return;

	expr = get_assigned_expr(ret_value->unop);
	if (!expr)
		return;
	if (expr->type != EXPR_CALL)
		return;

	sm_msg("warn: should this return really be negated?");
}

void check_return_negative_var(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
}
