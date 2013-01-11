/*
 * sparse/check_or_vs_and.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_function_hashtable.h"

static int my_id;

DEFINE_STRING_HASHTABLE_STATIC(unconstant_macros);

static int does_inc_dec(struct expression *expr)
{
	if (expr->type == EXPR_PREOP || expr->type == EXPR_POSTOP) {
		if (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT)
			return 1;
		return does_inc_dec(expr->unop);
	}
	return 0;
}

static int expr_equiv(struct expression *one, struct expression *two)
{
	struct symbol *one_sym, *two_sym;
	char *one_name = NULL;
	char *two_name = NULL;
	int ret = 0;

	if (does_inc_dec(one) || does_inc_dec(two))
		return 0;

	one_name = expr_to_str_sym_complex(one, &one_sym);
	if (!one_name || !one_sym)
		goto free;
	two_name = expr_to_str_sym_complex(two, &two_sym);
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
	sval_t sval;

	if (get_value(left->left, &sval)) {
		if (get_value(right->left, &sval))
			return expr_equiv(left->right, right->right);
		if (get_value(right->right, &sval))
			return expr_equiv(left->right, right->left);
		return 0;
	}
	if (get_value(left->right, &sval)) {
		if (get_value(right->left, &sval))
			return expr_equiv(left->left, right->right);
		if (get_value(right->right, &sval))
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

static int is_unconstant_macro(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return 0;
	if (search_unconstant_macros(unconstant_macros, macro))
		return 1;
	return 0;
}

static void match_condition(struct expression *expr)
{
	sval_t sval;

	if (expr->type != EXPR_BINOP)
		return;
	if (expr->op == '|') {
		if (get_value(expr->left, &sval) || get_value(expr->right, &sval))
			sm_msg("warn: suspicious bitop condition");
		return;
	}

	if (expr->op != '&')
		return;

	if (get_macro_name(expr->pos))
		return;
	if (is_unconstant_macro(expr->left) || is_unconstant_macro(expr->right))
		return;

	if ((get_value(expr->left, &sval) && sval.value == 0) ||
	    (get_value(expr->right, &sval) && sval.value == 0))
		sm_msg("warn: bitwise AND condition is false here");
}

static void match_binop(struct expression *expr)
{
	sval_t left, right, sval;

	if (expr->op != '&')
		return;
	if (!get_value(expr, &sval) || sval.value != 0)
		return;
	if (get_macro_name(expr->pos))
		return;
	if (!get_value(expr->left, &left) || !get_value(expr->right, &right))
		return;
	sm_msg("warn: odd binop '0x%llx & 0x%llx'", left.uvalue, right.uvalue);
}

void check_or_vs_and(int id)
{
	my_id = id;

	unconstant_macros = create_function_hashtable(100);
	load_strings("unconstant_macros", unconstant_macros);

	add_hook(&match_logic, LOGIC_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	if (option_spammy)
		add_hook(&match_binop, BINOP_HOOK);
}
