/*
 * Copyright (C) 2010 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "smatch.h"

static int my_id;

static int is_bool(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return 0;
	if (type_bits(type) == 1 && type->ctype.modifiers & MOD_UNSIGNED)
		return 1;
	return 0;
}

static int is_bool_from_context(struct expression *expr)
{
	sval_t sval;

	if (!get_implied_max(expr, &sval) || sval.uvalue > 1)
		return 0;
	if (!get_implied_min(expr, &sval) || sval.value < 0)
		return 0;
	return 1;
}

static int is_bool_op(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_PREOP && expr->op == '!')
		return 1;
	if (expr->type == EXPR_COMPARE)
		return 1;
	if (expr->type == EXPR_LOGICAL)
		return 1;
	return is_bool(expr);
}

static bool is_in_declaration(struct expression *expr)
{
	struct statement *stmt;
	int cnt = 0;

	/*
	 * I believe the problem is that Sparse occasionally evaluates stuff
	 * and that strips out unnecessary parentheses.  This is the same thing
	 * with converting sizeof() to just regular values.  I want the raw
	 * un-processed code.  I have complained about this before but someone
	 * on the Sparse list said that processed was awesome and I was a
	 * crazy person.  :P
	 */

	stmt = get_parent_stmt(expr);
	while (stmt) {
		if (stmt->type == STMT_DECLARATION)
			return true;
		stmt = stmt_get_parent_stmt(stmt);
		if (cnt++ > 5)
			return false;
	}
	return false;
}

static void match_condition(struct expression *expr)
{
	int print = 0;

	if (expr->type == EXPR_COMPARE) {
		if (expr->left->type == EXPR_COMPARE || expr->right->type == EXPR_COMPARE)
			print = 1;
		if (expr->left->type == EXPR_PREOP && expr->left->op == '!') {
			if (expr->left->unop->type == EXPR_PREOP && expr->left->unop->op == '!')
				return;
			if (expr->right->op == '!')
				return;
			if (is_bool(expr->right))
				return;
			if (is_bool(expr->left->unop))
				return;
			if (is_bool_from_context(expr->left->unop))
				return;
			print = 1;
		}
	}

	if (expr->type == EXPR_BINOP) {
		if (expr->left->type == EXPR_COMPARE || expr->right->type == EXPR_COMPARE)
			print = 1;
	}

	if (print) {
		sm_warning("add some parenthesis here?");
		return;
	}

	if (expr->type == EXPR_BINOP && expr->op == '&') {
		int i = 0;

		if (is_bool_op(expr->left))
			i++;
		if (is_bool_op(expr->right))
			i++;
		if (i == 1)
			sm_warning("maybe use && instead of &");
	}
}

static void match_binop(struct expression *expr)
{
	if (expr->op != '&')
		return;
	if (expr->left->op == '!')
		sm_warning("add some parenthesis here?");
}

static void match_mask(struct expression *expr)
{
	char *mask, *shift;

	if (expr->op != '&')
		return;
	if (expr->right->type != EXPR_BINOP)
		return;
	if (expr->right->op != SPECIAL_RIGHTSHIFT)
		return;

	if (is_in_declaration(expr))
		return;

	if (get_macro_name(expr->pos))
		return;

	mask = expr_to_str(expr->left);
	shift = expr_to_str(expr->right);
	sm_msg("warn: shift has higher precedence than mask '%s' & '%s'", mask, shift);
	free_string(shift);
	free_string(mask);
}

static void match_mask_compare(struct expression *expr)
{
	if (expr->op != '&' && expr->op != '|')
		return;
	if (expr->right->type != EXPR_COMPARE)
		return;

	sm_warning("compare has higher precedence than mask");
}

static void match_subtract_shift(struct expression *expr)
{
	if (is_in_declaration(expr))
		return;

	if (expr->op != SPECIAL_LEFTSHIFT)
		return;
	if (expr->right->type != EXPR_BINOP)
		return;
	if (expr->right->op != '-')
		return;
	sm_warning("subtract is higher precedence than shift");
}

void check_precedence(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_binop, BINOP_HOOK);
	add_hook(&match_mask, BINOP_HOOK);
	add_hook(&match_mask_compare, BINOP_HOOK);
	add_hook(&match_subtract_shift, BINOP_HOOK);
}
