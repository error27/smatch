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
 * This is for functions like this:
 *
 * int foo(int a)
 * {
 *        if (a >= 0 && a < 10) {
 *                 a = 42;
 *                 return 1;
 *        }
 *        return 0;
 * }
 *
 * If we pass in 5, it returns 1.
 *
 * It's a bit complicated because we can't just consider the final value, we
 * have to always consider the passed in value.
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

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

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &original;
}

static struct smatch_state *filter_my_sm(struct sm_state *sm)
{
	struct range_list *ret = NULL;
	struct sm_state *tmp;
	struct smatch_state *estate;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &merged)
			continue;
		if (tmp->state == &original) {
			estate = get_state_stree(tmp->pool, SMATCH_EXTRA, tmp->name, tmp->sym);
			if (!estate) {
//				sm_msg("debug: no value found in pool %p", tmp->pool);
				continue;
			}
		} else {
			estate = tmp->state;
		}
		ret = rl_union(ret, estate_rl(estate));
	} END_FOR_EACH_PTR(tmp);

	return alloc_estate_rl(ret);
}

struct smatch_state *get_orig_estate(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	struct smatch_state *state;

	sm = get_sm_state(my_id, name, sym);
	if (sm)
		return filter_my_sm(sm);

	state = get_state(SMATCH_EXTRA, name, sym);
	if (state)
		return state;
	return alloc_estate_rl(alloc_whole_rl(get_real_base_type(sym)));
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct stree *stree;
	struct sm_state *tmp;
	struct sm_state *my_sm;
	struct smatch_state *state;
	int param;

	stree = __get_cur_stree();

	FOR_EACH_MY_SM(SMATCH_EXTRA, stree, tmp) {
		if (!tmp->sym || !tmp->sym->ident || strcmp(tmp->name, tmp->sym->ident->name) != 0)
			continue;

		param = get_param_num_from_sym(tmp->sym);
		if (param < 0)
			continue;

		my_sm = get_sm_state(my_id, tmp->name, tmp->sym);
		if (!my_sm) {
			struct smatch_state *old;

			if (estate_is_whole(tmp->state))
				continue;
			old = get_state_stree(start_states, SMATCH_EXTRA, tmp->name, tmp->sym);
			if (old && estates_equiv(old, tmp->state))
				continue;
			sql_insert_return_states(return_id, return_ranges,
					LIMITED_VALUE, param, "$$",
					tmp->state->name);
			continue;
		}

		state = filter_my_sm(my_sm);
		if (!state)
			continue;
		/* This represents an impossible state.  I screwd up.  Bail. */
		if (!estate_rl(state))
			continue;
		if (estate_is_whole(state))
			continue;
		sql_insert_return_states(return_id, return_ranges,
					LIMITED_VALUE, param, "$$",
					state->name);
	} END_FOR_EACH_SM(tmp);
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct smatch_state *orig_vals;
	int param;

	param = get_param_num_from_sym(sym);
	if (param < 0)
		return;

	/* we are only saving params for now */
	if (!sym->ident || strcmp(name, sym->ident->name) != 0)
		return;

	orig_vals = get_orig_estate(name, sym);
	set_state(my_id, name, sym, orig_vals);
}

static void match_save_states(struct expression *expr)
{
	push_stree(&saved_stack, start_states);
	start_states = NULL;
}

static void match_restore_states(struct expression *expr)
{
	start_states = pop_stree(&saved_stack);
}

void register_param_limit(int id)
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

