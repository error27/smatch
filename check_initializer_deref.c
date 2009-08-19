/*
 * sparse/check_initializer_deref.c
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
	set_state(my_id, name, sym, &oktocheck);
}

static void record_dereferenced_vars(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (!expr || expr->type != EXPR_DEREF)
		return;

	expr = expr->deref;

	if (!strcmp(show_special(expr->op), "*"))  {
		name = get_variable_from_expr(expr->unop, &sym);
		if (name && sym) {
			set_state_expr(my_id, expr->unop, &derefed);
			add_modification_hook(name, &underef, NULL);
		}
		free_string(name);
	}
	record_dereferenced_vars(expr->unop);
}

static void match_declarations(struct symbol *sym)
{
	if (!sym->initializer)
		return;
	record_dereferenced_vars(sym->initializer);
}


static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;

	if (get_state_expr(my_id, expr) == &derefed) {
		smatch_msg("warn: variable derefenced in initializer '%s'",
			name);
		set_state_expr(my_id, expr, &oktocheck);
	}
free:
	free_string(name);
}


void check_initializer_deref(int id)
{
	my_id = id;
	set_default_state(my_id, &oktocheck);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
