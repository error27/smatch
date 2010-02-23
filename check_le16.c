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

static int is_le16(struct symbol *type)
{
	if (type->ctype.alignment != 2)
		return 0;
 	if (!(type->ctype.modifiers & MOD_UNSIGNED))
		return 0;
	return 1;
}

static void match_no_le16_param(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	struct symbol *type;
	char *name;

	arg = get_argument_from_call_expr(expr->args, (int)param);
	arg = strip_parens(arg);
	if (!arg)
		return;
	if (arg->type != EXPR_FORCE_CAST)
		return;

	type = get_type(arg);
	if (!is_le16(type))
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
	if (!option_spammy)
		return;
	my_id = id;
	register_funcs_from_file();
}
