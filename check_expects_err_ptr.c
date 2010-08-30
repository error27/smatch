/*
 * smatch/check_expects_err_ptr.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;
static struct symbol *func_sym;

STATE(argument);
STATE(ok);

static void set_ok(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(my_id, name, sym, &ok);
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	func_sym = sym;
	FOR_EACH_PTR(func_sym->ctype.base_type->arguments, arg) {
		if (!arg->ident) {
			continue;
		}
		set_state(my_id, arg->ident->name, arg, &argument);
	} END_FOR_EACH_PTR(arg);
}

static int get_arg_num(struct expression *expr)
{
	struct smatch_state *state;
	struct symbol *arg;
	struct symbol *this_arg;
	int i;

	expr = strip_expr(expr);
	if (expr->type != EXPR_SYMBOL)
		return -1;
	this_arg = expr->symbol;

	state = get_state_expr(my_id, expr);
	if (!state || state != &argument)
		return -1;
	
	i = 0;
	FOR_EACH_PTR(func_sym->ctype.base_type->arguments, arg) {
		if (arg == this_arg)
			return i;
		i++;
	} END_FOR_EACH_PTR(arg);

	return -1;
}

static void match_is_err(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	int arg_num;

	arg = get_argument_from_call_expr(expr->args, 0);
	arg_num = get_arg_num(arg);
	if (arg_num < 0)
		return;
	sm_msg("info: expects ERR_PTR %d", arg_num);
}

void check_expects_err_ptr(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	if (!option_info)
		return;

	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	set_default_modification_hook(my_id, &set_ok);
	add_function_hook("IS_ERR", &match_is_err, NULL);
}
