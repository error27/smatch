/*
 * sparse/smatch_param_filter.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(modified);
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

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	int param;

	param = get_param_num_from_sym(sym);
	if (param < 0)
		return;

	set_state(my_id, name, sym, &modified);
}

static const char *get_param_name(struct sm_state *sm)
{
	char *param_name;
	int name_len;
	static char buf[256];

	if (!sm->sym->ident)
		return NULL;

	param_name = sm->sym->ident->name;
	name_len = strlen(param_name);

	if (strcmp(sm->name, param_name) == 0) {
		return "$$";
	} else if (sm->name[name_len] == '-' && /* check for '-' from "->" */
	    strncmp(sm->name, param_name, name_len) == 0) {
		snprintf(buf, sizeof(buf), "$$%s", sm->name + name_len);
		return buf;
	} else if (sm->name[0] == '*' && strcmp(sm->name + 1, param_name) == 0) {
		return "*$$";
	}
	return NULL;
}

static char *get_orig_rl(struct sm_state *sm)
{
	struct range_list *ret = NULL;
	struct sm_state *tmp;
	struct smatch_state *extra;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state != &original)
			continue;
		extra = get_state_slist(tmp->pool, SMATCH_EXTRA, tmp->name, tmp->sym);
		if (!extra) {
			sm_msg("debug: no value found in pool %p", tmp->pool);
			return NULL;
		}
		ret = rl_union(ret, estate_rl(extra));
	} END_FOR_EACH_PTR(tmp);
	return show_ranges(ret);
}

static void print_one_mod_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm, struct state_list *slist)
{
	const char *param_name;
	char *filter;

	param_name = get_param_name(sm);
	if (!param_name)
		return;
	filter = get_orig_rl(sm);
	if (!filter)
		return;

	sm_msg("info: return_param_filter %d %d '%s' '%s' '%s' %s",
	       return_id, param, return_ranges,
	       param_name, filter, global_static());
}

static void print_one_extra_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm, struct state_list *slist)
{
	struct smatch_state *old;
	const char *param_name;

	old = get_state_slist(start_states, SMATCH_EXTRA, sm->name, sm->sym);
	if (old && estates_equiv(old, sm->state))
		return;

	param_name = get_param_name(sm);
	if (!param_name)
		return;

	sm_msg("info: return_param_filter %d %d '%s' '%s' '%s' %s",
	       return_id, param, return_ranges,
	       param_name, show_ranges(estate_rl(sm->state)), global_static());
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct state_list *extra_slist;
	struct sm_state *tmp;
	struct sm_state *sm;
	int param;

	extra_slist = get_all_states_slist(SMATCH_EXTRA, slist);

	FOR_EACH_PTR(extra_slist, tmp) {
		param = get_param_num_from_sym(tmp->sym);
		if (param < 0)
			continue;
		/*
		 * skip the parameter itself because that's handled by
		 * smatch_param_limit.c.
		 */
		if (tmp->sym->ident && strcmp(tmp->sym->ident->name, tmp->name) == 0)
			continue;

		sm = get_sm_state_slist(slist, my_id, tmp->name, tmp->sym);
		if (sm)
			print_one_mod_param(return_id, return_ranges, param, sm, slist);
		else
			print_one_extra_param(return_id, return_ranges, param, tmp, slist);
	} END_FOR_EACH_PTR(tmp);

	free_slist(&extra_slist);
}

void register_param_filter(int id)
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

