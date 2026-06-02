/*
 * Copyright (C) 2026 Dan Carpenter.
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

static void store_returned_hard_value(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *state;
	sval_t sval;

	if (!expr)
		return;

	if (get_value(expr, &sval)) {
		char buf[64];

		snprintf(buf, sizeof(buf), "%s", sval_to_str(sval));
		sql_insert_return_states(return_id, return_ranges, HARD_VALUE, -1, "$", buf);
		return;
	}

	state = get_state_expr(my_id, expr);
	if (!state || !estate_rl(state))
		return;
	sql_insert_return_states(return_id, return_ranges, HARD_VALUE, -1, "$", state->name);
}

static struct smatch_state *unmatched_empty(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static void set_empty(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(sm->owner, sm->name, sm->sym, alloc_estate_empty());
}

static void match_assign(struct expression *expr)
{
	struct smatch_state *state;
	sval_t sval;

	if (expr->op != '=')
		return;
	if (is_impossible_path())
		return;

	if (get_value(expr->right, &sval)) {
		set_state_expr(my_id, expr->left, alloc_estate_sval(sval));
		return;
	}

	state = get_state_expr(my_id, expr->right);
	if (!state)
		return;
	set_state_expr(my_id, expr->left, state);
}

static void extra_nomod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct smatch_state *mine;
	struct range_list *rl;

	mine = get_state(my_id, name, sym);
	if (!mine)
		return;

	rl = rl_intersection(estate_rl(mine), estate_rl(state));
	rl = cast_rl(estate_type(mine), rl);
	set_state(my_id, name, sym, alloc_estate_rl(rl));
}

static void set_hard_value(struct expression *expr, int param, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct expression *call;
	struct symbol *type;
	struct symbol *sym;
	char *name;

	if (param != -1 || strcmp(key, "$") != 0)
		return;

	if (expr->type != EXPR_ASSIGNMENT ||
	    expr->op != '=')
		return;

	call = get_rightmost_call(expr);
	if (!call)
		return;

	type = get_type(expr->left);
	call_results_to_rl(call, type, value, &rl);

	name = expr_to_var_sym(expr->left, &sym);
	if (!name || !sym)
		return;
	set_state(my_id, name, sym, alloc_estate_rl(rl));
	free_string(name);
}

void smatch_hard_value(int id)
{
	my_id = id;

	set_dynamic_states(my_id);

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_extra_nomod_hook(&extra_nomod_hook);

	add_merge_hook(my_id, &merge_estates);
	add_unmatched_state_hook(my_id, &unmatched_empty);
	add_modification_hook(my_id, &set_empty);

	add_split_return_callback(&store_returned_hard_value);
	select_return_states_hook(HARD_VALUE, &set_hard_value);
}
