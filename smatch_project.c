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
#include "smatch_function_hashtable.h"

static DEFINE_HASHTABLE_INSERT(insert_func, char, int);
static DEFINE_HASHTABLE_SEARCH(search_func, char, int);
static struct hashtable *silenced_funcs;
static struct hashtable *no_inline_funcs;

int is_silenced_function(void)
{
	char *func;

	func = get_function();
	if (!func)
		return 0;
	if (search_func(silenced_funcs, func))
		return 1;
	return 0;
}

int is_no_inline_function(const char *function)
{
	if (search_func(no_inline_funcs, (char *)function))
		return 1;
	return 0;
}

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

static void register_silenced_functions(void)
{
	struct token *token;
	char *func;
	char name[256];

	silenced_funcs = create_function_hashtable(500);

	if (option_project == PROJ_NONE)
		return;

	snprintf(name, 256, "%s.silenced_functions", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = alloc_string(show_ident(token->ident));
		insert_func(silenced_funcs, func, INT_PTR(1));
		token = token->next;
	}
	clear_token_alloc();
}

static void register_no_inline_functions(void)
{
	struct token *token;
	char *func;
	char name[256];

	no_inline_funcs = create_function_hashtable(500);

	if (option_project == PROJ_NONE)
		return;

	snprintf(name, 256, "%s.no_inline_functions", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = alloc_string(show_ident(token->ident));
		insert_func(no_inline_funcs, func, INT_PTR(1));
		token = token->next;
	}
	clear_token_alloc();
}

void register_project(int id)
{
	register_no_return_funcs();
	register_ignored_macros();
	register_silenced_functions();
	register_no_inline_functions();
}
