/*
 * smatch/check_passes_sizeof.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

#define NOBUF -2

static int my_id;

static int expr_equiv(struct expression *one, struct expression *two)
{
	struct symbol *one_sym, *two_sym;
	char *one_name = NULL;
	char *two_name = NULL;
	int ret = 0;

	if (!one || !two)
		return 0;
	if (one->type != two->type)
		return 0;

	one_name = get_variable_from_expr_complex(one, &one_sym);
	if (!one_name || !one_sym)
		goto free;
	two_name = get_variable_from_expr_complex(two, &two_sym);
	if (!two_name || !two_sym)
		goto free;
	if (one_sym != two_sym)
		goto free;
	if (strcmp(one_name, two_name) == 0)
		ret = 1;
free:
	free_string(one_name);
	free_string(two_name);
	return ret;
}

static struct expression *get_returned_expr(struct expression *expr)
{
	struct statement *stmt;

	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	if (!stmt || stmt->type != STMT_EXPRESSION)
		return NULL;
	if (stmt->expression->type != EXPR_ASSIGNMENT)
		return NULL;
	if (stmt->expression->right != expr)
		return NULL;
	return stmt->expression->left;
}

static struct expression *remove_dereference(struct expression *expr)
{
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return expr;
	expr = expr->unop;
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return expr;
	return expr->unop;
}

static int get_buf_number(struct expression *call, struct expression *size_arg)
{
	struct expression *arg;
	int idx = -1;

	size_arg = strip_expr(size_arg->cast_expression);
	size_arg = remove_dereference(size_arg);

	arg = get_returned_expr(call);
	if (arg && expr_equiv(arg, size_arg))
		return idx;

	FOR_EACH_PTR(call->args, arg) {
		idx++;
		if (expr_equiv(arg, size_arg))
			return idx;
	} END_FOR_EACH_PTR(arg);

	return NOBUF;
}

static void match_call(struct expression *call)
{
	struct expression *arg;
	char *name;
	int buf_nr;
	int i = -1;

	if (call->fn->type != EXPR_SYMBOL)
		return;

	name = get_variable_from_expr(call->fn, NULL);
	FOR_EACH_PTR(call->args, arg) {
		i++;
		if (arg->type != EXPR_SIZEOF)
			continue;
		buf_nr = get_buf_number(call, arg);
		if (buf_nr == NOBUF)
			sm_msg("info: sizeof_param '%s' %d", name, i);
		else
			sm_msg("info: sizeof_param '%s' %d %d", name, i, buf_nr);
	} END_FOR_EACH_PTR(arg);
	free_string(name);
}

void check_passes_sizeof(int id)
{
	if (!option_info)
		return;

	my_id = id;
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
