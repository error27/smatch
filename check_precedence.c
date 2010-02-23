/*
 * sparse/check_precedence.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static int is_bool(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return 0;
	if (type->bit_size == 1 && type->ctype.modifiers & MOD_UNSIGNED)
		return 1;
	return 0;
}

static void match_condition(struct expression *expr)
{
	int print = 0;

	if (expr->type == EXPR_COMPARE) {
		if (expr->left->type == EXPR_COMPARE || expr->right->type == EXPR_COMPARE)
			print = 1;
		if (expr->left->op == '!') {
			if (expr->left->type == EXPR_PREOP && expr->left->unop->op == '!')
				return;
			if (expr->right->op == '!')
				return;
			if (is_bool(expr->right))
				return;
			if (is_bool(expr->left->unop))
				return;
			print = 1;
		}
	}

	if (expr->type == EXPR_BINOP) {
		if (expr->left->type == EXPR_COMPARE || expr->right->type == EXPR_COMPARE)
			print = 1;
	}

	if (print)
		sm_msg("warn: add some parenthesis here?");
}

static void match_binop(struct expression *expr)
{
	if (expr->op != '&')
		return;
	if (expr->left->op == '!')
		sm_msg("warn: add some parenthesis here?");
}

void check_precedence(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_binop, BINOP_HOOK);
}
