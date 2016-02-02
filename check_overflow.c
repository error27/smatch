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

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

/*
 * This check has two smatch IDs.
 * my_used_id - keeps a record of array offsets that have been used.
 *              If the code checks that they are within bounds later on,
 *              we complain about using an array offset before checking
 *              that it is within bounds.
 */
static int my_used_id;

static void delete(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_used_id, sm->name, sm->sym, &undefined);
}

static int get_the_max(struct expression *expr, sval_t *sval)
{
	if (get_hard_max(expr, sval))
		return 1;
	if (!option_spammy)
		return 0;
	if (get_fuzzy_max(expr, sval))
		return 1;
	if (is_user_data(expr))
		return get_absolute_max(expr, sval);
	return 0;
}

static void array_check(struct expression *expr)
{
	struct expression *array_expr;
	int array_size;
	struct expression *offset;
	sval_t max;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	array_expr = get_array_base(expr);
	array_size = get_array_size(array_expr);
	if (!array_size || array_size == 1)
		return;

	offset = get_array_offset(expr);
	if (!get_the_max(offset, &max)) {
		if (getting_address())
			return;
		if (is_capped(offset))
			return;
		set_state_expr(my_used_id, offset, alloc_state_num(array_size));
	}
}

static void match_condition(struct expression *expr)
{
	int left;
	sval_t sval;
	struct state_list *slist;
	struct sm_state *tmp;
	int boundary;

	if (!expr || expr->type != EXPR_COMPARE)
		return;
	if (get_macro_name(expr->pos))
		return;
	if (get_implied_value(expr->left, &sval))
		left = 1;
	else if (get_implied_value(expr->right, &sval))
		left = 0;
	else
		return;

	if (left)
		slist = get_possible_states_expr(my_used_id, expr->right);
	else
		slist = get_possible_states_expr(my_used_id, expr->left);
	if (!slist)
		return;
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &merged || tmp->state == &undefined)
			continue;
		boundary = PTR_INT(tmp->state->data);
		boundary -= sval.value;
		if (boundary < 1 && boundary > -1) {
			char *name;

			name = expr_to_var(left ? expr->right : expr->left);
			sm_msg("error: testing array offset '%s' after use.", name);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

static void db_returns_buf_size(struct expression *expr, int param, char *unused, char *math)
{
	struct expression *call;
	struct symbol *left_type, *right_type;
	int bytes;
	sval_t sval;

	if (expr->type != EXPR_ASSIGNMENT)
		return;
	right_type = get_pointer_type(expr->right);
	if (!right_type || type_bits(right_type) != -1)
		return;

	call = strip_expr(expr->right);
	left_type = get_pointer_type(expr->left);

	if (!parse_call_math(call, math, &sval) || sval.value == 0)
		return;
	if (!left_type)
		return;
	bytes = type_bytes(left_type);
	if (bytes <= 0)
		return;
	if (sval.uvalue >= bytes)
		return;
	sm_msg("error: not allocating enough data %d vs %s", bytes, sval_to_str(sval));
}

void check_overflow(int id)
{
	my_used_id = id;
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	select_return_states_hook(BUF_SIZE, &db_returns_buf_size);
	add_modification_hook(my_used_id, &delete);
}
