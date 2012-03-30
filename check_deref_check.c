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
STATE(oktocheck);

static void underef(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(my_id, name, sym, &oktocheck);
}

static void match_dereference(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;
	if (getting_address())
		return;

	expr = strip_expr(expr->unop);
	if (implied_not_equal(expr, 0))
		return;
	set_state_expr(my_id, expr, &derefed);
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return;
	add_modification_hook(my_id, name, &underef, NULL);
	free_string(name);
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
	if (implied_not_equal(expr, 0))
		return;

	sm_msg("warn: variable dereferenced before check '%s' (see line %d)", sm->name, sm->line);
	set_state_expr(my_id, expr, &oktocheck);
}

void check_deref_check(int id)
{
	my_id = id;
	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
