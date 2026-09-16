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

static int my_id;
static struct expression *ai_expr;

static void print_user_rl(struct expression *expr)
{
	struct range_list *rl = NULL;
	char *name;

	expr = strip_parens(expr);
	if (expr->type == EXPR_BINOP) {
		print_user_rl(expr->left);
		print_user_rl(expr->right);
		return;
	}
	if (expr->type == EXPR_VALUE || expr->type == EXPR_SIZEOF)
		return;

	name = expr_to_str(expr);
	get_user_rl(expr, &rl);
	sm_printf(" %s=%s", name, rl ? show_rl(rl) : "none");
	free_string(name);
}

static void print_ai_overflow_info(void)
{
	if (!ai_expr)
		return;

	sm_prefix();
	sm_printf("user_rl:");
	print_user_rl(ai_expr);
	sm_printf("\n");
}

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

static bool is_size_t(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (type != &ulong_ctype)
		return false;

	if (expr->type == EXPR_BINOP) {
		if (is_size_t(expr->left))
			return true;
		if (is_size_t(expr->right))
			return true;
	}

	expr = strip_parens(expr);
	if (expr->type == EXPR_SIZEOF)
		return true;

	if (expr->type == EXPR_CAST)
		type = expr->cast_type;

	type = get_type(expr);
	return false;
}

static void match_binop(struct expression *expr)
{
	struct range_list *left_rl = NULL;
	struct range_list *right_rl = NULL;
	struct symbol *type;
	char *str;

	if (expr->op != '*' && expr->op != '+')
		return;

	if (!is_size_t(expr))
		return;

	if (get_user_rl(expr->left, &left_rl) &&
	    !user_rl_capped(expr->left))
		get_absolute_rl(expr->right, &right_rl);
	else if (get_user_rl(expr->right, &right_rl) &&
		 !user_rl_capped(expr->right))
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
	ai_expr = expr;
	sm_warning("potential user controlled size_t overflow '%s' '%s %s %s' type='%s'",
		   str, show_rl(left_rl), show_special(expr->op), show_rl(right_rl),
		   type_to_str(type));
	ai_expr = NULL;
	free_string(str);
}

void check_integer_overflow_size_t(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(match_binop, BINOP_HOOK);
	register_ai_info(my_id, print_ai_overflow_info);
}
