/*
 * sparse/smatch_project.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * This file is only for very generic stuff, that is reusable
 * between projects.  If you need something special create a
 * check_your_project.c.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static void register_no_return_funcs(void)
{
	struct token *token;
	const char *func;
	char name[256];

	if (option_project == PROJ_NONE)
		strcpy(name, "no_return_funcs");
	else
		snprintf(name, 256, "%s.no_return_funcs", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		add_function_hook(func, &__match_nullify_path_hook, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

static void return_implies(struct expression *call_expr, int param, char *key, char *value)
{
	struct range_list *rl;
	struct expression *arg;

	if (call_expr->type == EXPR_ASSIGNMENT)
		call_expr = strip_expr(call_expr->right);

	arg = get_argument_from_call_expr(call_expr->args, param);
	get_value_ranges(value, &rl);
	set_extra_expr_nomod(arg, alloc_estate_range_list(rl));
}

static void register_ignored_macros(void)
{
	struct token *token;
	char *macro;
	char name[256];

	if (option_project == PROJ_NONE)
		strcpy(name, "ignored_macros");
	else
		snprintf(name, 256, "%s.ignored_macros", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		macro = alloc_string(show_ident(token->ident));
		add_ptr_list(&__ignored_macros, macro);
		token = token->next;
	}
	clear_token_alloc();
}

void register_project(int id)
{
	register_no_return_funcs();
	register_ignored_macros();
	add_db_return_implies_callback(RANGE_CAP, &return_implies);
}
