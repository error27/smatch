/*
 * sparse/check_err_ptr.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static int err_ptr = 0;
static int returns_null = 0;

static void match_err_ptr(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr)
		return;
	if (expr->type != EXPR_CALL)
		return;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;
	if (!strcmp(expr->fn->symbol->ident->name, "ERR_PTR"))
		err_ptr = 1;
}

extern int check_assigned_expr_id;
static void match_return(struct expression *ret_value)
{
	struct state_list *slist;
	struct sm_state *tmp;
	sval_t sval;

	match_err_ptr(ret_value);
	slist = get_possible_states_expr(check_assigned_expr_id, ret_value);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &undefined || tmp->state == &merged)
			continue;
		match_err_ptr((struct expression *)tmp->state->data);
	} END_FOR_EACH_PTR(tmp);

	if (get_implied_value_sval(ret_value, &sval)) {
		if (sval.value == 0)
			returns_null = 1;
	}
}

static void match_end_func(struct symbol *sym)
{
	if (err_ptr)
		sm_info("returns_err_ptr");
	err_ptr = 0;
	returns_null = 0;
}

void check_err_ptr(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	if (!option_info)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
