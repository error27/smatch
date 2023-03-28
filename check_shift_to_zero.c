/*
 * Copyright (C) 2014 Oracle.
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

static void match_binop(struct expression *expr)
{
	struct symbol *type;
	sval_t bits;

	if (expr->op != SPECIAL_RIGHTSHIFT)
		return;

	if (!get_implied_value(expr->right, &bits))
		return;

	type = get_type(expr->left);
	if (!type)
		return;
	if (type_bits(type) == -1 || type_bits(type) > bits.value)
		return;
	if (is_ignored_expr(my_id, expr))
		return;
	sm_warning("right shifting more than type allows %d vs %lld", type_bits(type), bits.value);
}

static void match_binop2(struct expression *expr)
{
	struct expression *left;
	struct expression *tmp;
	sval_t mask, shift;
	char *macro, *inner;

	if (expr->op != SPECIAL_RIGHTSHIFT)
		return;

	left = strip_expr(expr->left);
	tmp = get_assigned_expr(left);
	if (tmp)
		left = tmp;
	if (left->type != EXPR_BINOP || left->op != '&')
		return;

	if (!get_value(expr->right, &shift))
		return;
	if (!get_value(left->right, &mask))
		return;

	if (mask.uvalue >> shift.uvalue)
		return;

	macro = get_macro_name(expr->pos);
	inner = get_inner_macro(expr->pos);
	if (macro && inner) {
		if (strcmp(macro, "unlikely") == 0)
			goto warn;
		if (strcmp(inner, "unlikely") == 0 ||
		    strcmp(inner, "__const_hweight8") == 0 ||
		    strcmp(inner, "BITS_PER_LONG") == 0 ||
		    strcmp(inner, "CORDIC_PRECISION_SHIFT") == 0)
			return;
	}

warn:
	sm_warning("mask and shift to zero: expr='%s'", expr_to_str(expr));
}

static void match_assign(struct expression *expr)
{
	struct symbol *type;
	sval_t bits;

	if (expr->op != SPECIAL_SHR_ASSIGN)
		return;

	if (!get_implied_value(expr->right, &bits))
		return;
	type = get_type(expr->left);
	if (!type)
		return;
	if (type_bits(type) > bits.value)
		return;
	sm_warning("right shift assign to zero");
}

void check_shift_to_zero(int id)
{
	my_id = id;

	add_hook(&match_binop, BINOP_HOOK);
	add_hook(&match_binop2, BINOP_HOOK);

	add_hook(&match_assign, ASSIGNMENT_HOOK);

}
