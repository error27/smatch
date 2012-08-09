/*
 * sparse/check_deref_check.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

STATE(derefed);

static void underef(struct sm_state *sm)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

static void match_dereference(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	if (getting_address())
		return;
	expr = strip_expr(expr->unop);
	if (implied_not_equal(expr, 0))
		return;

	set_state_expr(my_id, expr, &derefed);
}

static void set_param_dereferenced(struct expression *arg, char *unused)
{
	if (implied_not_equal(arg, 0))
		return;
	set_state_expr(my_id, arg, &derefed);
}

static void match_condition(struct expression *expr)
{
	struct sm_state *sm;

	if (__in_pre_condition)
		return;

	if (get_macro_name(expr->pos))
		return;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm || sm->state != &derefed)
		return;

	sm_msg("warn: variable dereferenced before check '%s' (see line %d)", sm->name, sm->line);
	set_state_expr(my_id, expr, &undefined);
}

void check_deref_check(int id)
{
	my_id = id;
	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_db_fn_call_callback(DEREFERENCE, &set_param_dereferenced);
	add_modification_hook(my_id, &underef);
}
