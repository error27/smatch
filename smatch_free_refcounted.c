/*
 * Copyright 2026 Dan Carpenter
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

STATE(freed);

static void return_freed_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state != &merged && tmp->state != &undefined)
			goto found;
	} END_FOR_EACH_PTR(tmp);

	return;
found:
	sql_insert_return_states(return_id, return_ranges, FREED_REFCOUNTED, param,
				 printed_name, tmp->state->name);
}

static bool all_conditions_true(void)
{
	static int cond_id = -1;
	struct sm_state *tmp;

	if (cond_id == -1)
		cond_id = id_from_name("smatch_stored_conditions");

	FOR_EACH_MY_SM(cond_id, __get_cur_stree(), tmp) {
		if (tmp->state == &false_state)
			return false;
	} END_FOR_EACH_SM(tmp);

	return true;
}

static struct sm_state *get_decremented_refcount(void)
{
	static int refcount_id = -1;
	struct sm_state *tmp;

	if (refcount_id == -1)
		refcount_id = id_from_name("smatch_refcount");

	FOR_EACH_MY_SM(refcount_id, __get_cur_stree(), tmp) {
		if (strcmp(tmp->state->name, "dec") == 0)
			return tmp;
	} END_FOR_EACH_SM(tmp);

	return NULL;
}

static void match_free_helper(struct expression *expr, const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *refcount_sm;
	const char *refcount_name;
	struct symbol *orig_sym;
	char *orig_name;
	int param;

	if (!all_conditions_true())
		return;

	refcount_sm = get_decremented_refcount();
	if (!refcount_sm)
		return;

	orig_name = get_param_var_sym_var_sym_early(name, sym, NULL, &orig_sym);
	if (!orig_name || !orig_sym)
		return;
	if (refcount_sm->sym != orig_sym)
		return;

	param = get_param_key_from_var_sym(orig_name, orig_sym, NULL, NULL);
	if (param < 0)
		return;

	param = get_param_key_from_var_sym(refcount_sm->name, refcount_sm->sym,
					   NULL, &refcount_name);
	if (param < 0)
		return;

	set_state(my_id, orig_name, orig_sym, alloc_state_str(refcount_name));
}

static void match_free(struct expression *expr, const char *name, struct symbol *sym)
{
	match_free_helper(expr, name, sym, &freed);
}

void smatch_free_refcounted(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	set_dynamic_states(my_id);
	add_free_hook(match_free);
	add_return_info_callback(my_id, return_freed_callback);
}
