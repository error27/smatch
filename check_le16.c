/*
 * smatch/check_le16.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_no_le16_param(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	char *macro_name;
	char *name;

	arg = get_argument_from_call_expr(expr->args, (int)param);
	if (!arg)
		return;
	macro_name = get_macro_name(&arg->pos);
	if (!macro_name || strcmp(macro_name, "cpu_to_le16"))
		return;

	arg = strip_expr(arg);
	name = get_variable_from_expr_complex(arg, NULL);
	sm_msg("warn: don't need to call cpu_to_le16() for '%s'", name);
	free_string(name);
}

static void register_funcs_from_file(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.no_le16");
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
		add_function_hook(func, &match_no_le16_param, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_le16(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	register_funcs_from_file();
}
