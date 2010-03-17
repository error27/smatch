/*
 * smatch/check_return.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static void must_check(const char *fn, struct expression *expr, void *data)
{
	struct statement *stmt;

	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	if (stmt->type == STMT_EXPRESSION && stmt->expression == expr)
		sm_msg("warn: unchecked '%s'", fn);
}

static void register_must_check_funcs(void)
{
	struct token *token;
	const char *func;
	static char name[256];


	snprintf(name, 256, "%s.must_check_funcs", option_project_str);
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
		add_function_hook(func, &must_check, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void check_return(int id)
{
	my_id = id;
	register_must_check_funcs();
}
