/*
 * sparse/check_err_ptr_deref.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(err_ptr);
STATE(checked);

static void check_is_err_ptr(struct sm_state *sm)
{
	if (slist_has_state(sm->possible, &err_ptr)) {
		smatch_msg("error: '%s' dereferencing possible ERR_PTR()",
			   sm->name);
		set_state(sm->name, my_id, sm->sym, &checked);
	}
}

static void match_returns_err_ptr(const char *fn, struct expression *expr,
				void *info)
{
	char *left_name = NULL;
	struct symbol *left_sym;

	left_name = get_variable_from_expr(expr->left, &left_sym);
	if (!left_name || !left_sym)
		goto free;
	set_state(left_name, my_id, left_sym, &err_ptr);
free:
	free_string(left_name);
}

static void match_is_err(const char *fn, struct expression *expr,
				void *data)
{
	char *name;
	struct symbol *sym;

	expr = get_argument_from_call_expr(expr->args, 0);
	if (expr->type == EXPR_ASSIGNMENT)
		expr = expr->left;
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	set_cond_states(name, my_id, sym, &err_ptr, &checked);
free:
	free_string(name);
}

static void match_dereferences(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;
	struct sm_state *sm;

	deref = get_variable_from_expr(expr->deref->unop, &sym);
	if (!deref || !sym)
		goto free;

	sm = get_sm_state(deref, my_id, sym);
	if (sm)
		check_is_err_ptr(sm);
free:
        free_string(deref);
}

static void register_err_ptr_funcs(void)
{
	struct token *token;
	const char *func;

	token = get_tokens_file("kernel.returns_err_ptr");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		add_function_assign_hook(func, &match_returns_err_ptr, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void check_err_ptr_deref(int id)
{
	my_id = id;
	add_conditional_hook("IS_ERR", &match_is_err, NULL);
	register_err_ptr_funcs();
	add_hook(&match_dereferences, DEREF_HOOK);
}
