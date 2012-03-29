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

struct param_implication {
	int param;
	struct range_list *rl;
};

static void match_param_implication(const char *fn, struct expression *call_expr,
				    struct expression *assign_expr, void *_imp)
{
	struct param_implication *imp = _imp;
	struct expression *arg;

	arg = get_argument_from_call_expr(call_expr->args, imp->param);
	set_extra_expr_nomod(arg, alloc_estate_range_list(clone_range_list(imp->rl)));
}

static void add_param_implication(const char *func, int param, char *range, char *ret_range)
{
	struct range_list *ret_rl = NULL;
	struct range_list *rl = NULL;
	struct param_implication *imp;

	get_value_ranges(ret_range, &ret_rl);

	get_value_ranges(range, &rl);
	rl = clone_permanent(rl);

	imp = malloc(sizeof(*imp));
	imp->param = param;
	imp->rl = rl;

	printf("%s returning %lld-%lld implies %s\n", func, rl_min(ret_rl), rl_max(ret_rl), show_ranges(rl));

	return_implies_state(func, rl_min(ret_rl), rl_max(ret_rl), &match_param_implication, imp);
}

static void register_parameter_implications(void)
{
	char name[256];
	struct token *token;
	const char *func;
	int param;
	char *range;
	char *ret_range;

	snprintf(name, 256, "%s.parameter_implications", option_project_str);
	name[255] = '\0';
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

		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		param = atoi(token->number);

		token = token->next;
		if (token_type(token) != TOKEN_STRING)
			return;
		range = token->string->data;

		token = token->next;
		if (token_type(token) != TOKEN_STRING)
			return;
		ret_range = token->string->data;


		add_param_implication(func, param, range, ret_range);

		token = token->next;
	}
	clear_token_alloc();
}

void register_project(int id)
{
	register_no_return_funcs();
	register_ignored_macros();
	register_parameter_implications();
}
