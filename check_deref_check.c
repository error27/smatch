/*
 * sparse/check_deref_check.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

STATE(derefed);
STATE(oktocheck);

static void underef(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(name, my_id, sym, &oktocheck);
}

static void match_dereference(struct expression *expr)
{
	char *name;
	struct symbol *sym;

	if (strcmp(show_special(expr->deref->op), "*"))
		return;

	name = get_variable_from_expr(expr->deref->unop, &sym);
	if (!name || !sym)
		goto free;
	set_state(name, my_id, sym, &derefed);
	add_modification_hook(name, &underef, NULL);
free:
	free_string(name);
}


static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;

	if (get_state(name, my_id, sym) == &derefed) {
		smatch_msg("warning: variable derefenced before check '%s'",
			name);
		set_state(name, my_id, sym, &oktocheck);
	}
free:
	free_string(name);
}


void check_deref_check(int id)
{
	my_id = id;
	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
