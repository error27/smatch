/*
 * Copyright (C) 2022 Oracle.
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

STATE(error_code);

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	struct sm_state *sm;
	sval_t sval;

	sm = get_sm_state(SMATCH_EXTRA, cur->name, cur->sym);
	if (!sm || !estate_rl(sm->state))
		return;
	if (type_unsigned(estate_type(sm->state)))
		return;
	sval = estate_min(sm->state);
	if (sval.value == 0)
		set_state(my_id, cur->name, cur->sym, &undefined);
}

static bool is_error_macro(struct expression *expr)
{
	char *macro;
	sval_t sval;

	if (!expr)
		return false;
	if (expr->type != EXPR_PREOP || expr->op != '-')
		return false;

	if (!get_value(expr, &sval))
		return false;
	if (sval.value < -4095 || sval.value >= 0)
		return false;

	expr = expr->unop;
	macro = get_macro_name(expr->pos);
	if (!macro || macro[0] != 'E')
		return false;

	return true;
}

static void match_assign(struct expression *expr)
{
	if (expr->op != '=')
		return;

	if (!is_error_macro(expr->right))
		return;

	set_state_expr(my_id, expr->left, &error_code);
}

static void match_condition(struct expression *expr)
{
	if (expr->type != EXPR_COMPARE)
		return;
	if (expr->op != '<' && expr->op != SPECIAL_LTE)
		return;
	if (!expr_is_zero(expr->right))
		return;

	set_true_false_states_expr(my_id, expr->left, NULL, &undefined);
}

static bool is_empty_state(struct expression *expr)
{
	struct smatch_state *state;

	state = get_extra_state(expr);
	if (!state)
		return false;
	if (estate_rl(state))
		return false;
	return true;
}

bool holds_kernel_error_codes(struct expression *expr)
{
	struct range_list *rl;

	if (!expr)
		return false;

	if (is_error_macro(expr))
		return true;

	if (is_empty_state(expr))
		return false;

	if (get_implied_rl(expr, &rl)) {
		if (rl_min(rl).value >= 0 &&
		    rl_max(rl).uvalue <= INT_MAX)
			return false;

		if (type_positive_bits(rl_type(rl)) >= 63 &&
		    !sval_is_negative(rl_min(rl)) &&
		    rl_max(rl).uvalue <= UINT_MAX)
			return false;
	}

	return expr_has_possible_state(my_id, expr, &error_code);
}

static void match_return(int return_id, char *return_ranges, struct expression *expr)
{
	if (!expr)
		return;

	if (!holds_kernel_error_codes(expr))
		return;

	sql_insert_return_states(return_id, return_ranges, NEGATIVE_ERROR,
				 -1, "$", "");
}

static void set_error_code(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &error_code);
}

void check_returns_negative_error_code(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_modification_hook(my_id, &set_undefined);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_split_return_callback(&match_return);
	select_return_param_key(NEGATIVE_ERROR, &set_error_code);
}
