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

extern struct expression_list *big_expression_stack;

static int my_id;

STATE(derefed);
STATE(oktocheck);

static void underef(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(my_id, name, sym, &oktocheck);
}

static int is_not_really_dereference()
{
	struct expression *tmp;
	int i = 0;
	int dot_ops = 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (!i++)
			continue;
		if (tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
		if (tmp->op == '&')
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static void match_dereference(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;
	if (is_not_really_dereference())
		return;

	expr = strip_expr(expr->unop);
	if (implied_not_equal(expr, 0))
		return;
	set_state_expr(my_id, expr, &derefed);
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return;
	add_modification_hook(name, &underef, NULL);
	free_string(name);
}

static void match_condition(struct expression *expr)
{
	if (get_state_expr(my_id, expr) == &derefed) {
		char *name;

		name = get_variable_from_expr(expr, NULL);
		if (!implied_not_equal(expr, 0))
			sm_msg("warn: variable dereferenced before check '%s'", name);
		set_state_expr(my_id, expr, &oktocheck);
		free_string(name);
	}
}

void check_deref_check(int id)
{
	my_id = id;
	set_default_state(my_id, &oktocheck);
	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
