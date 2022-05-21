/*
 * Copyright (C) 2010 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
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
static struct hashtable *skipped_funcs;
static struct hashtable *skipped_macros;
static struct hashtable *silenced_funcs;
static struct hashtable *no_inline_funcs;

static unsigned long skipped;

void set_function_skipped(void)
{
	skipped = true;
}

int is_skipped_function(void)
{
	return skipped;
}

static void match_function_def(struct symbol *sym)
{
	char *macro;
	char *func;

	func = get_function();
	if (!func)
		return;
	if (skipped_funcs && search_func(skipped_funcs, func)) {
		set_function_skipped();
		return;
	}
	macro = get_macro_name(cur_func_sym->pos);
	if (macro && skipped_macros && search_func(skipped_macros, macro)) {
		set_function_skipped();
		return;
	}
}

/*
 * A silenced function will still be processed and potentially appear in info
 * output, but not regular checks.
 */
int is_silenced_function(void)
{
	char *func;

	if (is_skipped_function())
		return 1;

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

static struct hashtable *register_skipped(const char *filename)
{
	struct hashtable *table;
	struct token *token;
	char *func;
	char name[256];

	if (option_project == PROJ_NONE)
		return NULL;

	snprintf(name, 256, "%s.%s", option_project_str, filename);

	token = get_tokens_file(name);
	if (!token)
		return NULL;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return NULL;

	table = create_function_hashtable(500);

	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return table;
		func = alloc_string(show_ident(token->ident));
		insert_func(table, func, INT_PTR(1));
		token = token->next;
	}
	clear_token_alloc();

	return table;
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
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_function_data(&skipped);
	register_no_return_funcs();
	register_ignored_macros();
	skipped_funcs = register_skipped("skipped_functions");
	skipped_macros = register_skipped("skipped_macros");
	register_silenced_functions();
	register_no_inline_functions();
}
