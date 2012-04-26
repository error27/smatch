/*
 * sparse/check_or_vs_and.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static int expr_equiv(struct expression *one, struct expression *two)
{
	struct symbol *one_sym, *two_sym;
	char *one_name = NULL;
	char *two_name = NULL;
	int ret = 0;

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

static int inconsistent_check(struct expression *left, struct expression *right)
{
	long long val;

	if (get_value(left->left, &val)) {
		if (get_value(right->left, &val))
			return expr_equiv(left->right, right->right);
		if (get_value(right->right, &val))
			return expr_equiv(left->right, right->left);
		return 0;
	}
	if (get_value(left->right, &val)) {
		if (get_value(right->left, &val))
			return expr_equiv(left->left, right->right);
		if (get_value(right->right, &val))
			return expr_equiv(left->left, right->left);
		return 0;
	}

	return 0;
}

static void check_or(struct expression *expr)
{
	if (expr->left->type != EXPR_COMPARE ||
			expr->left->op != SPECIAL_NOTEQUAL)
		return;
	if (expr->right->type != EXPR_COMPARE ||
			expr->right->op != SPECIAL_NOTEQUAL)
		return;
	if (!inconsistent_check(expr->left, expr->right))
		return;

	sm_msg("warn: was && intended here instead of ||?");
}

static void check_and(struct expression *expr)
{
	if (expr->left->type != EXPR_COMPARE ||
			expr->left->op != SPECIAL_EQUAL)
		return;
	if (expr->right->type != EXPR_COMPARE ||
			expr->right->op != SPECIAL_EQUAL)
		return;
	if (!inconsistent_check(expr->left, expr->right))
		return;

	sm_msg("warn: was || intended here instead of &&?");
}

static void match_logic(struct expression *expr)
{
	if (expr->type != EXPR_LOGICAL)
		return;

	if (expr->op == SPECIAL_LOGICAL_OR)
		check_or(expr);
	if (expr->op == SPECIAL_LOGICAL_AND)
		check_and(expr);
}

void check_or_vs_and(int id)
{
	my_id = id;

	add_hook(&match_logic, LOGIC_HOOK);
}
