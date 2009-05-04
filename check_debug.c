/*
 * sparse/check_debug.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"  // blast this was supposed to be internal only stuff

static int my_id;

static void match_all_values(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;

	slist = get_all_states(SMATCH_EXTRA);
	__print_slist(slist);
	free_slist(&slist);
}

static void match_print_value(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;
	struct sm_state *tmp;
	struct expression *arg_expr;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	if (arg_expr->type != EXPR_STRING) {
		smatch_msg("error:  the argument to %s is supposed to be a string literal", fn);
		return;
	}
	
	slist = get_all_states(SMATCH_EXTRA);
	FOR_EACH_PTR(slist, tmp) {
		if (!strcmp(tmp->name, arg_expr->string->data))
			smatch_msg("%s = %s", tmp->name, tmp->state->name);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}


void check_debug(int id)
{
	my_id = id;
	add_function_hook("__smatch_all_values", &match_all_values, NULL);
	add_function_hook("__smatch_print_value", &match_print_value, NULL);
}
