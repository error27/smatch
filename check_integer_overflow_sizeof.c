/*
 * Copyright 2024 Linaro Ltd.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static bool is_part_of_overflow_check(struct expression *expr)
{
	struct expression *parent;
	struct expression *left;

	while (expr && expr->type != EXPR_BINOP)
		expr = expr_get_parent_expr(expr);

	parent = expr_get_parent_expr(expr);
	while (parent && parent->type == EXPR_PREOP && parent->op == '(')
		parent = expr_get_parent_expr(parent);

	if (!parent || parent->type != EXPR_COMPARE)
		return false;
	if (parent->op != '<' &&
	    parent->op != SPECIAL_LTE &&
	    parent->op != SPECIAL_UNSIGNED_LT &&
	    parent->op != SPECIAL_UNSIGNED_LTE)
		return false;

	left = strip_expr(parent->left);
	if (left != expr)
		return false;

	if (expr_equiv(expr->left, parent->right))
		return true;
	if (expr_equiv(expr->right, parent->right))
		return true;
	return false;
}

static bool gets_cast_to_pointer(struct expression *expr)
{
	int count = 0;

	while ((expr = expr_get_parent_expr(expr))) {
		if (expr->type == EXPR_CAST &&
		    type_is_ptr(get_type(expr)))
			return true;
		if (++count > 10)
			return false;
	}
	return false;
}

static bool is_sizeof(struct expression *expr)
{
	expr = strip_parens(expr);
	return expr->type == EXPR_SIZEOF;
}

static void match_binop(struct expression *expr)
{
	struct range_list *left_rl = NULL;
	struct range_list *right_rl = NULL;
	struct symbol *type;
	char *str;

	if (__in_builtin_overflow_func)
		return;
	if (db_incomplete())
		return;

	if (expr->op != '*' && expr->op != '+')
		return;

	if (type_is_ptr(get_type(expr->left)))
		return;

	if (!is_sizeof(expr->left) && !is_sizeof(expr->right))
		return;

	if (get_user_rl(expr->left, &left_rl) &&
	    !user_rl_capped(expr->left) &&
	    !is_overflow_safe_variable(expr->left)) {
		get_absolute_rl(expr->right, &right_rl);
	} else if (get_user_rl(expr->right, &right_rl) &&
		 !user_rl_capped(expr->right) &&
		 !is_overflow_safe_variable(expr->right))
		get_absolute_rl(expr->left, &left_rl);
	else
		return;

	if (!sval_binop_overflows(rl_max(left_rl), expr->op, rl_max(right_rl)))
		return;

	if (is_part_of_overflow_check(expr))
		return;
	if (gets_cast_to_pointer(expr))
		return;

	type = rl_type(left_rl);
	if (type_positive_bits(rl_type(right_rl)) > type_positive_bits(type))
		type = rl_type(right_rl);
	if (type_positive_bits(type) < 31)
		type = &int_ctype;

	str = expr_to_str(expr);
	sm_warning("potential user controlled sizeof overflow '%s' '%s %s %s'",
		   str, show_rl(left_rl), show_special(expr->op), show_rl(right_rl));
	free_string(str);
}

void check_integer_overflow_sizeof(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(match_binop, BINOP_HOOK);
}
