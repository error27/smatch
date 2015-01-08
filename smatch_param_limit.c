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
 * This is almost the same as smatch_param_filter.c.  The difference is that
 * this only deals with values passed on the stack and param filter only deals
 * with values changed so that the caller sees the new value.  It other words
 * the key for these should always be "$" and the key for param_filter should
 * never be "$".  Also smatch_param_set() should never use "$" as the key.
 * Param set should work together with param_filter to determine the value that
 * the caller sees at the end.
 *
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

static struct stree *start_states;
static struct stree_stack *saved_stack;

static void save_start_states(struct statement *stmt)
{
	struct smatch_state *state;
	struct symbol *tmp;

	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (!tmp->ident)
			continue;
		state = get_state(SMATCH_EXTRA, tmp->ident->name, tmp);
		if (!state)
			state = alloc_estate_whole(get_real_base_type(tmp));
		set_state_stree(&start_states, SMATCH_EXTRA, tmp->ident->name, tmp, state);
	} END_FOR_EACH_PTR(tmp);
}

static void free_start_states(void)
{
	free_stree(&start_states);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;

	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (state)
		return state;
	return alloc_estate_whole(get_real_base_type(sm->sym));
}

struct smatch_state *get_orig_estate(const char *name, struct symbol *sym)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (state)
		return state;

	state = get_state(SMATCH_EXTRA, name, sym);
	if (state)
		return state;
	return alloc_estate_rl(alloc_whole_rl(get_real_base_type(sym)));
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *start_state, *state;
	struct symbol *tmp;
	int param;

	param = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		param++;
		if (!tmp->ident)
			continue;
		state = get_state(my_id, tmp->ident->name, tmp);
		if (state)
			goto print;
		state = get_state(SMATCH_EXTRA, tmp->ident->name, tmp);
		if (state)
			goto print;
		continue;
print:
		if (estate_is_whole(state))
			continue;

		start_state = get_state_stree(start_states, SMATCH_EXTRA, tmp->ident->name, tmp);
		if (estates_equiv(state, start_state))
			continue;
//		sm_msg("return_range %s limited '%s' from %s to %s", return_ranges, tmp->ident->name, start_state->name, state->name);
		sql_insert_return_states(return_id, return_ranges,
				LIMITED_VALUE, param, "$", state->name);
	} END_FOR_EACH_PTR(tmp);
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct smatch_state *orig_vals;
	int param;

	param = get_param_num_from_sym(sym);
	if (param < 0)
		return;

	/* we only save on-stack params */
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
	free_stree(&start_states);
	start_states = pop_stree(&saved_stack);
}

void register_param_limit(int id)
{
	my_id = id;

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_hook(&free_start_states, END_FUNC_HOOK);

	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);

	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);

	add_split_return_callback(&print_return_value_param);
}

