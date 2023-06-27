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
 * The PARAM_LIMIT code is for functions like this:
 *
 * int frob(int a)
 * {
 *        if (a >= 0 && a < 10) {
 *                 a = 42;
 *                 return 1;
 *        }
 *        return 0;
 * }
 *
 * If frob() returns 1, then we know that a must have been in the 0-9 range
 * at the start.  Or if we return 0 then a is outside that range.  So if the
 * caller passes a 5 then the function must return 1.
 *
 * The "a" variable gets set to 42 in the middle, but we don't care about
 * that, we only care about the passed in value.
 *
 * Originally, this code looked at the starting stree and the final stree and
 * asked was this variable set part way through?  (Over simplification).  This
 * approach works, but the problem is that it produces too many results.  For
 * example, if we dereference a pointer then we know that it must be non-NULL.
 * So that was recorded as a PARAM_LIMIT.  Another thing is that if we have
 * two callers and we do "if (caller_one) return true; else return false;".  The
 * if (caller_one) condition might have implications so maybe it sets twenty
 * different states instead of just the one.
 *
 * These extra PARAM_LIMITs are still correct, but the problem is that they
 * take up space in the database.  Perhaps more importantly parsing PARAM_LIMITS
 * is very expensive because we have to figure out the implications for that.
 * (2023: idea: Parse all the PARAM_LIMITS at once instead of sequentially).
 * The PARAM_LIMITS create a lot of sm_state history and use a lot of resources.
 *
 * So we need a more deliberate approach.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static struct stree *limit_states;
static struct stree *ignore_states;

int __no_limits;

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;

	if (!param_was_set_var_sym(sm->name, sm->sym)) {
		state = __get_state(SMATCH_EXTRA, sm->name, sm->sym);
		if (state)
			return state;
	}
	return alloc_estate_whole(estate_type(sm->state));
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

static struct range_list *generify_mtag_range(struct smatch_state *state)
{
	struct range_list *rl;
	struct data_range *drange;

	if (!estate_type(state) || estate_type(state)->type != SYM_PTR)
		return estate_rl(state);

	/*
	 * The problem is that we get too specific on our param limits when we
	 * know exactly what pointers are passed to a function.  It gets to the
	 * point where we say "pointer x will succeed, but everything else will
	 * fail."  And then we introduce a new caller which passes a different
	 * pointer and it's like, "Sorry bro, that's not possible."
	 *
	 */
	rl = estate_rl(state);
	FOR_EACH_PTR(rl, drange) {
		if (drange->min.value != drange->max.value)
			continue;
		if (drange->min.value == 0)
			continue;
		if (is_err_ptr(drange->min))
			continue;
		return rl_union(valid_ptr_rl, rl);
	} END_FOR_EACH_PTR(drange);

	return estate_rl(state);
}

static bool sm_was_set(struct sm_state *sm)
{
	struct relation *rel;

	if (!estate_related(sm->state))
		return param_was_set_var_sym(sm->name, sm->sym);

	FOR_EACH_PTR(estate_related(sm->state), rel) {
		if (param_was_set_var_sym(sm->name, sm->sym))
			return true;
	} END_FOR_EACH_PTR(rel);
	return false;
}

static bool is_boring_pointer_info(const char *name, struct range_list *rl)
{
	char *rl_str;

	/* addresses are always boring */
	if (name[0] == '&')
		return true;

	/*
	 * One way that PARAM_LIMIT can be set is by dereferencing pointers.
	 * It's not necessarily very valuable to track that a pointer must
	 * be non-NULL.  It's even less valuable to know that it's either NULL
	 * or valid.  It can be nice to know that it's not an error pointer, I
	 * suppose.  But let's not pass that data back to all the callers
	 * forever.
	 *
	 */

	if (strlen(name) < 40)
		return false;

	rl_str = show_rl(rl);
	if (!rl_str)
		return false;

	if (strcmp(rl_str, "4096-ptr_max") == 0 ||
	    strcmp(rl_str, "0,4096-ptr_max") == 0)
		return true;

	return false;
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *state, *old;
	struct sm_state *tmp;
	struct range_list *rl;
	const char *orig_name, *key;
	struct symbol *sym;
	int param;

	FOR_EACH_MY_SM(SMATCH_EXTRA, __get_cur_stree(), tmp) {
		if (tmp->name[0] == '&')
			continue;

		if (!get_state_stree(limit_states, my_id, tmp->name, tmp->sym) &&
		    get_state_stree(ignore_states, my_id, tmp->name, tmp->sym))
			continue;

		orig_name = get_param_var_sym_var_sym(tmp->name, tmp->sym, NULL, &sym);
		if (!orig_name || !sym)
			continue;
		param = get_param_key_from_var_sym(orig_name, sym, NULL, &key);
		if (param < 0)
			continue;

		state = __get_state(my_id, orig_name, sym);
		if (!state) {
			if (sm_was_set(tmp))
				continue;
			state = tmp->state;
		}

		if (estate_is_whole(state) || estate_is_empty(state))
			continue;
		old = get_state_stree(get_start_states(), SMATCH_EXTRA, tmp->name, tmp->sym);
		if (old && rl_equiv(estate_rl(old), estate_rl(state)))
			continue;

		if (is_ignored_kernel_data(key))
			continue;

		rl = generify_mtag_range(state);
		if (is_boring_pointer_info(key, rl))
			continue;

		sql_insert_return_states(return_id, return_ranges, PARAM_LIMIT,
					 param, key, show_rl(rl));
	} END_FOR_EACH_SM(tmp);
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct smatch_state *orig;
	struct symbol *param_sym;
	char *param_name;

	if (expr && expr->smatch_flags & Fake)
		return;

	param_name = get_param_var_sym_var_sym(name, sym, NULL, &param_sym);
	if (!param_name || !param_sym)
		goto free;
	if (get_param_num_from_sym(param_sym) < 0)
		goto free;

	/* already saved */
	if (get_state(my_id, param_name, param_sym))
		goto free;

	if (__in_buf_clear)
		return;

	orig = get_state(SMATCH_EXTRA, param_name, param_sym);
	if (!orig)
		orig = alloc_estate_whole(estate_type(state));

	set_state(my_id, param_name, param_sym, orig);
free:
	free_string(param_name);
}

static void extra_nomod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	if (__no_limits) {
		set_state_stree(&ignore_states, my_id, name, sym, &undefined);
		return;
	}
	set_state_stree(&limit_states, my_id, name, sym, &undefined);
}

static void match_end_func(struct symbol *sym)
{
	free_stree(&ignore_states);
	free_stree(&limit_states);
}

void register_param_limit(int id)
{
	my_id = id;

	add_function_data((unsigned long *)&limit_states);
	add_function_data((unsigned long *)&ignore_states);
	add_hook(&match_end_func, END_FUNC_HOOK);

	db_ignore_states(my_id);
	set_dynamic_states(my_id);

	add_extra_mod_hook(&extra_mod_hook);
	add_extra_nomod_hook(&extra_nomod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);

	add_split_return_callback(&print_return_value_param);
}

