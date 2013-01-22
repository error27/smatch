/*
 * sparse/check_free.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * check_memory() is getting too big and messy.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(freed);
STATE(ok);

static void ok_to_use(struct sm_state *sm)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static int is_freed(struct expression *expr)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &freed))
		return 1;
	return 0;
}

static void match_symbol(struct expression *expr)
{
	char *name;

	if (!is_freed(expr))
		return;
	name = expr_to_var(expr);
	sm_msg("warn: '%s' was already freed.", name);
	free_string(name);
}

static void match_dereferences(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;
	expr = strip_expr(expr->unop);

	if (!is_freed(expr))
		return;
	name = expr_to_var_sym(expr, NULL);
	sm_msg("error: dereferencing freed memory '%s'", name);
	set_state_expr(my_id, expr, &ok);
	free_string(name);
}

static void match_free(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(param));
	if (!arg)
		return;
	/* option_spammy already prints a warning here */
	if (!option_spammy && is_freed(arg)) {
		char *name = expr_to_var_sym(arg, NULL);

		sm_msg("error: double free of '%s'", name);
		free_string(name);
	}
	set_state_expr(my_id, arg, &freed);
}

void check_free(int id)
{
	my_id = id;

	if (option_project == PROJ_KERNEL)
		add_function_hook("kfree", &match_free, (void *)0);
	else
		add_function_hook("free", &match_free, (void *)0);

	if (option_spammy)
		add_hook(&match_symbol, SYM_HOOK);
	else
		add_hook(&match_dereferences, DEREF_HOOK);

	add_modification_hook(my_id, &ok_to_use);
}
