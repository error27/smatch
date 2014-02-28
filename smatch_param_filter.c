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
 * void foo(int *x)
 * {
 * 	if (*x == 42)
 *		*x = 0;
 * }
 *
 * The final value of *x depends on the input to the function but with *x == 42
 * filtered out.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(modified);
STATE(original);

static struct stree *start_states;
static struct stree_stack *saved_stack;
static void save_start_states(struct statement *stmt)
{
	start_states = get_all_states_stree(SMATCH_EXTRA);
}

static void match_end_func(void)
{
	free_stree(&start_states);
}

static void match_save_states(struct expression *expr)
{
	push_stree(&saved_stack, start_states);
	start_states = NULL;
}

static void match_restore_states(struct expression *expr)
{
	free_stree(&start_states);
	start_states = pop_stree(&saved_stack);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &original;
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	int param;

	param = get_param_num_from_sym(sym);
	if (param < 0)
		return;

	set_state(my_id, name, sym, &modified);
}

static struct range_list *get_orig_rl(struct sm_state *sm)
{
	struct range_list *ret = NULL;
	struct sm_state *tmp;
	struct smatch_state *extra;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state != &original)
			continue;
		extra = get_state_stree(tmp->pool, SMATCH_EXTRA, tmp->name, tmp->sym);
		if (!extra) {
//			sm_msg("debug: no value found in pool %p", tmp->pool);
			return NULL;
		}
		ret = rl_union(ret, estate_rl(extra));
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static void print_one_mod_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm)
{
	const char *param_name;
	struct range_list *rl;

	param_name = get_param_name(sm);
	if (!param_name)
		return;
	rl = get_orig_rl(sm);
	if (is_whole_rl(rl))
		return;

	sql_insert_return_states(return_id, return_ranges, FILTER_VALUE, param,
			param_name, show_rl(rl));
}

static void print_one_extra_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm)
{
	struct smatch_state *old;
	const char *param_name;

	if (estate_is_whole(sm->state))
		return;
	old = get_state_stree(start_states, SMATCH_EXTRA, sm->name, sm->sym);
	if (old && estates_equiv(old, sm->state))
		return;

	param_name = get_param_name(sm);
	if (!param_name)
		return;

	sql_insert_return_states(return_id, return_ranges, FILTER_VALUE, param,
			param_name, sm->state->name);
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct stree *stree;
	struct sm_state *tmp;
	struct sm_state *sm;
	int param;

	stree = __get_cur_stree();

	FOR_EACH_MY_SM(SMATCH_EXTRA, stree, tmp) {
		param = get_param_num_from_sym(tmp->sym);
		if (param < 0)
			continue;
		/*
		 * skip the parameter itself because that's handled by
		 * smatch_param_limit.c.
		 */
		if (tmp->sym->ident && strcmp(tmp->sym->ident->name, tmp->name) == 0)
			continue;

		sm = get_sm_state(my_id, tmp->name, tmp->sym);
		if (sm)
			print_one_mod_param(return_id, return_ranges, param, sm);
		else
			print_one_extra_param(return_id, return_ranges, param, tmp);
	} END_FOR_EACH_SM(tmp);
}

void register_param_filter(int id)
{
	my_id = id;

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_split_return_callback(&print_return_value_param);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);
}

