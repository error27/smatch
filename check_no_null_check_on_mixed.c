/*
 * Copyright (C) 2018 Oracle.
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

STATE(null);

static bool is_assigned_function(struct expression *expr)
{
	struct expression *assigned;
	struct sm_state *sm, *tmp;

	sm = get_assigned_sm(expr);
	if (!sm)
		return false;
	FOR_EACH_PTR(sm->possible, tmp) {
		assigned = tmp->state->data;
		if (!assigned)
			continue;
		if (assigned->type != EXPR_CALL)
			return false;
	} END_FOR_EACH_PTR(tmp);
	return true;
}

static void deref_hook(struct expression *expr)
{
	struct smatch_state *estate;
	struct sm_state *sm;
	char *name;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm || !slist_has_state(sm->possible, &null))
		return;
	if (implied_not_equal(expr, 0))
		return;
	estate = get_state_expr(SMATCH_EXTRA, expr);
	if (estate_is_empty(estate))
		return;
	if (is_impossible_path())
		return;

	if (!option_spammy &&
	    !is_assigned_function(expr))
		return;

	name = expr_to_str(expr);
	sm_msg("warn: '%s' can also be NULL", name);
	free_string(name);
}

static void match_condition(struct expression *expr)
{
	struct data_range *drange;
	struct expression *arg;
	struct sm_state *sm, *tmp;

	expr = strip_expr(expr);
	if (expr->type != EXPR_CALL)
		return;
	if (!sym_name_is("IS_ERR", expr->fn))
		return;

	arg = get_argument_from_call_expr(expr->args, 0);
	arg = strip_expr(arg);

	if (!arg || implied_not_equal(arg, 0))
		return;

	sm = get_sm_state_expr(SMATCH_EXTRA, arg);
	if (!sm)
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (!estate_rl(tmp->state))
			continue;
		drange = first_ptr_list((struct ptr_list *)estate_rl(tmp->state));
		if (drange->min.value == 0 && drange->max.value == 0)
			goto has_null;
	} END_FOR_EACH_PTR(tmp);

	return;

has_null:
	set_true_false_states_expr(my_id, arg, NULL, &null);
}

void check_no_null_check_on_mixed(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_condition, CONDITION_HOOK);
	add_modification_hook(my_id, &set_undefined);
	add_dereference_hook(&deref_hook);
}
