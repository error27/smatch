/*
 * smatch/check_resource_size.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;
extern int check_assigned_expr_id;

static int is_probably_ok(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_BINOP)
		return 1;
	if (expr->type == EXPR_SIZEOF)
		return 1;

	return 0;
}

static void verify_size_expr(struct expression *expr)
{
	if (expr->type != EXPR_BINOP)
		return;
	if (expr->op != '-')
		return;
	if (is_probably_ok(expr->left))
		return;
	if (is_probably_ok(expr->right))
		return;
	sm_msg("warn: consider using resource_size() here");
}

static void handle_assigned_expr(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(check_assigned_expr_id, expr);
	if (!state || !state->data)
		return;
	expr = (struct expression *)state->data;
	verify_size_expr(expr);
}

static void match_resource(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *arg_expr;
	int arg_no = (int)_arg_no;

	arg_expr = get_argument_from_call_expr(expr->args, arg_no);
	arg_expr = strip_expr(arg_expr);

	if (arg_expr->type == EXPR_SYMBOL) {
		handle_assigned_expr(arg_expr);
		return;
	}
	verify_size_expr(arg_expr);
}

void check_resource_size(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_hook("ioremap_nocache", &match_resource, (void *)1);
	add_function_hook("ioremap", &match_resource, (void *)1);
	add_function_hook("__request_region", &match_resource, (void *)2);
	add_function_hook("__release_region", &match_resource, (void *)2);
	add_function_hook("__devm_request_region", &match_resource, (void *)3);
	add_function_hook("__devm_release_region", &match_resource, (void *)3);
}
