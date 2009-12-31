/*
 * sparse/check_dma_on_stack.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_dma_func(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	struct symbol *sym;
	char *name;

	arg = get_argument_from_call_expr(expr->args, (int)param);
	arg = strip_expr(arg);
	if (!arg)
		return;
	if (arg->type == EXPR_PREOP && arg->op == '&') {
		if (arg->unop->type != EXPR_SYMBOL)
			return;
		name = get_variable_from_expr(arg, NULL);
		sm_msg("error: doing dma on the stack (%s)", name);
		free_string(name);
		return;
	}
	if (arg->type != EXPR_SYMBOL)
		return;
	sym = get_type(arg);
	if (!sym || sym->type != SYM_ARRAY)
		return;
	name = get_variable_from_expr(arg, NULL);
	sm_msg("error: doing dma on the stack (%s)", name);
	free_string(name);
}

static void register_funcs_from_file(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.dma_funcs");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		arg = atoi(token->number);
		add_function_hook(func, &match_dma_func, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_dma_on_stack(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	register_funcs_from_file();
}
