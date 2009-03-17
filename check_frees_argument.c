/*
 * sparse/check_frees_argument.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/* 
 * This script is for finding functions like hcd_buffer_free() which free
 * their arguments.  After running it, add those functions to check_memory.c
 */

#include "smatch.h"

static int my_id;

static struct symbol *this_func;

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

static void print_if_arg(char *name, struct symbol *sym)
{
	struct symbol *arg;
	const char *arg_name;
	int i = 0;

	FOR_EACH_PTR(this_func->ctype.base_type->arguments, arg) {
		arg_name = (arg->ident?arg->ident->name:"-");
		if (sym == arg && !strcmp(name, arg_name))
			smatch_msg("info: frees argument %d", i);
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void match_call(struct expression *expr)
{
	char *fn_name;
	struct expression *tmp;
	struct symbol *sym;
	char *name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name || strcmp(fn_name, "kfree"))
		goto free;

	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		name = get_variable_from_expr(tmp, &sym);
		print_if_arg(name, sym);
		free_string(name);
	} END_FOR_EACH_PTR(tmp);
free:
	free_string(fn_name);
}

void register_frees_argument(int id)
{
	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
