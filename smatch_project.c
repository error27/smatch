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

static void register_no_return_funcs(void)
{
	struct token *token;
	const char *func;
	static char name[256];


	snprintf(name, 256, "%s.no_return_funcs", option_project_str);
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
		add_function_hook(func, &__match_nullify_path_hook, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void register_project(int id)
{
	if (option_project != PROJ_KERNEL)
		add_function_hook("exit", &__match_nullify_path_hook, NULL);
	register_no_return_funcs();
}
