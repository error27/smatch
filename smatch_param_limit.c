/*
 * sparse/smatch_param_limit.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(original);

static struct state_list *start_states;
static void save_start_states(struct statement *stmt)
{
	start_states = get_all_states(SMATCH_EXTRA);
}

static void match_end_func(void)
{
	free_slist(&start_states);
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
			estate = get_state_slist(tmp->pool, SMATCH_EXTRA, tmp->name, tmp->sym);
			if (!estate) {
				sm_msg("debug: no value found in pool %p", tmp->pool);
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

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct state_list *my_slist;
	struct sm_state *tmp;
	struct smatch_state *state;
	int param;

	my_slist = get_all_states_slist(my_id, slist);

	FOR_EACH_PTR(my_slist, tmp) {
		param = get_param_num_from_sym(tmp->sym);
		if (param < 0)
			continue;
		state = filter_my_sm(tmp);
		if (!state)
			continue;
		/* This represents an impossible state.  I screwd up.  Bail. */
		if (!estate_rl(state))
			continue;
		sm_msg("info: return_param_limit %d %d '%s' '$$' '%s' %s",
		       return_id, param, return_ranges,
		       state->name, global_static());
	} END_FOR_EACH_PTR(tmp);

	free_slist(&my_slist);
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

void register_param_limit(int id)
{
	if (!option_info)
		return;

	my_id = id;

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_returned_state_callback(&print_return_value_param);
	add_hook(&match_end_func, END_FUNC_HOOK);
}

