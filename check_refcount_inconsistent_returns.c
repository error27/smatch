/*
 * Copyright (C) 2021 Oracle.
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

STATE(inc);
STATE(dec);

static void match_inc(struct expression *expr, const char *name, struct symbol *sym)
{
	if (has_possible_state(my_id, name, sym, &dec)) {
		set_state(my_id, name, sym, &undefined);
		return;
	}

	set_state(my_id, name, sym, &inc);
}

static void match_dec(struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &dec);
}

// drivers/pcmcia/ds.c:598 pcmcia_device_add() warn: inconsistent refcounting 's->dev.kobj.kref.refcount.refs.counter':
// check that parent is gone

static void check_count(const char *name, struct symbol *sym)
{
	struct stree *stree, *orig;
	struct sm_state *return_sm;
	struct range_list *dec_lines = NULL;
	struct range_list *inc_lines = NULL;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	int success_path_increments = 0;
	int success_path_unknown = 0;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		if (parent_is_gone_var_sym(name, sym))
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);

		if (success_fail_return(estate_rl(return_sm->state)) == RET_SUCCESS) {
			if (sm && strcmp(sm->state->name, "inc") == 0)
				success_path_increments++;
			else
				success_path_unknown++;
			goto swap_stree;
		}

		if (!sm)
			goto swap_stree;

		if (sm->state == &inc)
			add_range(&inc_lines, line, line);
		else if (sm->state == &dec)
			add_range(&dec_lines, line, line); /* dec or &undefined */

swap_stree:
		__swap_cur_stree(orig);
	} END_FOR_EACH_PTR(stree);

	if (!success_path_increments || success_path_unknown)
		return;
	if (!dec_lines || !inc_lines)
		return;

	sm_warning("inconsistent refcounting '%s':", name);
	sm_printf("  inc on: %s\n", show_rl(inc_lines));
	sm_printf("  dec on: %s\n", show_rl(dec_lines));
}

static void process_states(void)
{
	struct sm_state *tmp;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		check_count(tmp->name, tmp->sym);
	} END_FOR_EACH_SM(tmp);
}

static void warn_on_leaks(const char *name, struct symbol *sym)
{
	struct stree *stree, *orig;
	struct sm_state *return_sm;
	struct range_list *inc_lines = NULL;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	bool has_dec = false;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		if (parent_is_gone_var_sym(name, sym))
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);
		if (!sm)
			goto swap_stree;

		if (slist_has_state(sm->possible, &dec)) {
			has_dec = true;
			goto swap_stree;
		}

		if (success_fail_return(estate_rl(return_sm->state)) == RET_FAIL &&
		    sm->state == &inc)
			add_range(&inc_lines, line, line);
swap_stree:
		__swap_cur_stree(orig);
	} END_FOR_EACH_PTR(stree);

	if (!has_dec || !inc_lines)
		return;

	sm_warning("refcount leak '%s': lines='%s'", name, show_rl(inc_lines));
}

static void process_states2(void)
{
	struct sm_state *tmp;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		warn_on_leaks(tmp->name, tmp->sym);
	} END_FOR_EACH_SM(tmp);
}

void check_refcount_inconsistent_returns(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	preserve_out_of_scope(id);

	add_refcount_init_hook(&match_inc);
	add_refcount_inc_hook(&match_inc);
	add_refcount_dec_hook(&match_dec);

	all_return_states_hook(&process_states);
	all_return_states_hook(&process_states2);
}
