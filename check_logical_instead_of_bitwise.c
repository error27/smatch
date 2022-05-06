/*
 * Copyright (C) 2012 Oracle.
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
#include "smatch_extra.h"

static int my_id;

static int is_bitshift(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type != EXPR_BINOP)
		return 0;
	if (expr->op == SPECIAL_LEFTSHIFT)
		return 1;
	return 0;
}

static int warn_if_both_known_values(struct expression *expr)
{
	sval_t left, right;

	if (get_macro_name(expr->pos))
		return 0;

	if (!get_value(expr->right, &left) ||
	    !get_value(expr->left, &right))
		return 0;

	if ((left.value == 0 || left.value == 1) &&
	    (right.value == 0 || right.value == 1))
		return 0;

	sm_warning("should this be a bitwise op?");
	return 1;
}

static int check_is_mask(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type != EXPR_BINOP)
		return 0;
	if (expr->op != SPECIAL_LEFTSHIFT &&
	    expr->op != SPECIAL_RIGHTSHIFT &&
	    expr->op != '|')
		return 0;
	if (implied_not_equal(expr, 0))
		return 1;
	return 0;
}

static void match_logic(struct expression *expr)
{
	struct range_list *rl;

	if (expr->type != EXPR_LOGICAL)
		return;

	if (warn_if_both_known_values(expr))
		return;

	if (!check_is_mask(expr->right))
		return;

	/* check for logic, by looking for bool */
	get_absolute_rl(expr->left, &rl);
	if (rl_min(rl).value >= 0 && rl_max(rl).value <= 1)
		return;

	sm_warning("should this be a bitwise op?");
}

static void match_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->type != EXPR_LOGICAL)
		return;
	if (is_bitshift(right->left) || is_bitshift(right->right))
		sm_warning("should this be a bitwise op?");
}

static int is_bool(struct expression *expr)
{
	struct range_list *rl;

	get_absolute_rl(expr, &rl);

	if (rl_min(rl).value != 0 && rl_min(rl).value != 1)
		return 0;
	if (rl_max(rl).value != 0 && rl_max(rl).value != 1)
		return 0;

	return 1;
}

static void match_assign_mask(struct expression *expr)
{
	struct expression *right;

	if (!option_spammy)
		return;

	if (expr->op != SPECIAL_AND_ASSIGN &&
	    expr->op != SPECIAL_OR_ASSIGN)
		return;

	if (is_bool(expr->left))
		return;

	right = strip_expr(expr->right);
	if (right->type != EXPR_PREOP || right->op != '!')
		return;
	if (is_bool(right->unop))
		return;
	sm_warning("should this be a bitwise negate mask?");
}

static void match_logical_negate(struct expression *expr)
{
	sval_t sval;

	if (expr->op != '!')
		return;
	expr = strip_expr(expr->unop);
	if (expr->type != EXPR_BINOP || expr->op != SPECIAL_LEFTSHIFT)
		return;
	if (!get_value(expr->left, &sval) || sval.value != 1)
		return;
	sm_warning("potential ! vs ~ typo");
}

void check_logical_instead_of_bitwise(int id)
{
	my_id = id;

	add_hook(&match_logic, LOGIC_HOOK);
	add_hook(&match_logical_negate, OP_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_assign_mask, ASSIGNMENT_HOOK);
}
