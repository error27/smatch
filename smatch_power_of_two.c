/*
 * Copyright (C) 2021
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
#include "smatch_slist.h"

static int my_id;

STATE(power_of_two);

bool is_power_of_two(struct expression *expr)
{
	sval_t sval;

	expr = strip_expr(expr);

	if (expr->type == EXPR_BINOP &&
	    expr->op == SPECIAL_LEFTSHIFT &&
	    is_power_of_two(expr->left))
		return true;

	if (get_implied_value(expr, &sval)) {
		if (!(sval.uvalue & (sval.uvalue - 1)))
			return true;
		return false;
	}

	if (get_state_expr(my_id, expr) == &power_of_two)
		return true;

	return false;
}

static bool is_sign_expansion(struct expression *expr)
{
	struct range_list *rl;
	struct symbol *type_left;
	struct symbol *type_right;

	type_left = get_type(expr->left);
	type_right = get_type(expr->right);
	if (!type_left || !type_right)
		return true;
	if (type_bits(type_left) <= type_bits(type_right))
		return false;

	get_absolute_rl(expr->right, &rl);
	if (sval_is_negative(rl_min(rl)))
		return true;

	return false;
}

static void match_assign(struct expression *expr)
{
	if (expr->op != '=')
		return;

	if (is_sign_expansion(expr))
		return;

	if (is_power_of_two(expr->right))
		set_state_expr(my_id, expr->left, &power_of_two);
}

static bool is_minus_mask(struct expression *left, struct expression *right)
{
	if (right->type != EXPR_BINOP ||
	    right->op != '-')
		return false;

	if (right->right->value != 1)
		return false;

	if (expr_equiv(left, right->left))
		return true;

	return false;
}

static void match_condition(struct expression *expr)
{
	expr = strip_expr(expr);
	if (expr->type != EXPR_BINOP ||
	    expr->op != '&')
		return;

	if (is_minus_mask(strip_expr(expr->left), strip_expr(expr->right))) {
		set_true_false_states_expr(my_id, expr->left, NULL, &power_of_two);
		return;
	}

	if (is_minus_mask(strip_expr(expr->right), strip_expr(expr->left))) {
		set_true_false_states_expr(my_id, expr->right, NULL, &power_of_two);
		return;
	}
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (sm->state != &power_of_two)
		return;

	sql_insert_caller_info(call, POWER_OF_TWO, param, printed_name, "");
}

static void set_power_of_two(const char *name, struct symbol *sym, char *value)
{
	set_state(my_id, name, sym, &power_of_two);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	struct smatch_state *estate;
	sval_t sval;

	if (sm->state != &power_of_two)
		return;

	if (param != -1 && !param_was_set_var_sym(sm->name, sm->sym))
		return;

	estate = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (estate_get_single_value(estate, &sval))
		return;

	sql_insert_return_states(return_id, return_ranges, POWER_OF_TWO_SET, param, printed_name, "");
}

static void returns_power_of_two_set(struct expression *expr, int param, char *key, char *value)
{
	char *name;
	struct symbol *sym;

	name = get_name_sym_from_key(expr, param, key, &sym);
	if (!name)
		return;
	set_state(my_id, name, sym, &power_of_two);
}

void register_power_of_two(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_caller_info_callback(my_id, caller_info_callback);
	select_caller_name_sym(set_power_of_two, POWER_OF_TWO);
	add_return_info_callback(my_id, return_info_callback);
	select_return_states_hook(POWER_OF_TWO_SET, &returns_power_of_two_set);
}
