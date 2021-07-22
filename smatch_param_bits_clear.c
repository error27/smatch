/*
 * Copyright (C) 2021 Oracle
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

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return alloc_bstate(-1ULL, -1ULL);
}

static void match_assign(struct expression *expr)
{
	sval_t sval;

	if (expr->type != EXPR_ASSIGNMENT)
		return;

	if (expr->op != SPECIAL_AND_ASSIGN)
		return;

	if (!get_implied_value(expr->right, &sval))
		return;

	set_state_expr(my_id, expr->left, alloc_bstate(0, sval.uvalue));
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	unsigned long long bits_clear;
	char buffer[64];
	struct smatch_state *estate;
	struct bit_info *binfo;
	sval_t sval;

	estate = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (estate_get_single_value(estate, &sval))
		return;

	binfo = sm->state->data;
	bits_clear = binfo->possible;

	if (bits_clear == 0)
		return;

	sprintf(buffer, "0x%llx", bits_clear);
	sql_insert_return_states(return_id, return_ranges, BIT_CLEAR, param, printed_name, buffer);
}

struct smatch_state *merge_bstates_clear(struct smatch_state *one_state,
					 struct smatch_state *two_state)
{
	struct bit_info *one, *two;

	one = one_state->data;
	two = two_state->data;

	if (binfo_equiv(one, two))
		return one_state;

	return alloc_bstate(0, one->possible | two->possible);
}

void register_param_bits_clear(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_bstates);

	add_return_info_callback(my_id, return_info_callback);
}
