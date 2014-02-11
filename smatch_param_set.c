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

/*
 * This is for functions like:
 *
 * int foo(int *x)
 * {
 * 	if (*x == 42) {
 *		*x = 0;
 *		return 1;
 *	}
 * 	return 0;
 * }
 *
 * If we return 1 that means the value of *x has been set to 0.  If we return
 * 0 then we have left *x alone.
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	set_state(my_id, name, sym, state);
}

static void print_one_return_value_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm, char *implied_rl)
{
	const char *param_name;

	param_name = get_param_name(sm);
	if (!param_name)
		return;
	if (strcmp(param_name, "$$") == 0)
		return;

	sql_insert_return_states(return_id, return_ranges, ADDED_VALUE, param,
			param_name, implied_rl);
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct state_list *my_slist;
	struct sm_state *sm;
	struct smatch_state *extra;
	int param;
	struct range_list *rl;

	my_slist = get_all_states(my_id);

	FOR_EACH_PTR(my_slist, sm) {
		if (!estate_rl(sm->state))
			continue;
		extra = get_state(SMATCH_EXTRA, sm->name, sm->sym);
		if (!estate_rl(extra))
			continue;
		rl = rl_intersection(estate_rl(sm->state), estate_rl(extra));
		if (!rl)
			continue;

		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;
		if (!sm->sym->ident)
			continue;
		print_one_return_value_param(return_id, return_ranges, param, sm, show_rl(rl));
	} END_FOR_EACH_PTR(sm);
}

void register_param_set(int id)
{
	my_id = id;

	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);
	add_split_return_callback(&print_return_value_param);
}

