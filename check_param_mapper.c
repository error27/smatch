/*
 * sparse/check_param_mapper.c
 *
 * Copyright (C) 2009 Your Name.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The idea behind this test is that if we have:
 * void foo(int bar)
 * {
 *         baz(1, bar);
 * }
 *
 * Passing "bar" to foo() really means passing "bar" to baz();
 * 
 * In this case it would print:
 * info: param_mapper 0 => bar 1
 *
 */

#include "smatch.h"

static int my_id;

STATE(argument);

static struct symbol *func_sym;

static void delete(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	delete_state(my_id, name, sym);
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
		add_modification_hook(arg->ident->name, &delete, NULL);
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

static void match_call(struct expression *expr)
{
	struct expression *tmp;
	char *func;
	int arg_num;
	int i;

	if (expr->fn->type != EXPR_SYMBOL)
		return;

	func = expr->fn->symbol_name->name;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		arg_num = get_arg_num(tmp);
		if (arg_num >= 0)
			sm_msg("info: param_mapper %d => %s %d", arg_num, func, i);
		i++;
	} END_FOR_EACH_PTR(tmp);
}

void check_param_mapper(int id)
{
	if (!option_param_mapper)
		return;
	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
