/*
 * sparse/smatch_param_set.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
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

static const char *get_param_name(struct sm_state *sm)
{
	char *param_name;
	int name_len;
	static char buf[256];

	param_name = sm->sym->ident->name;
	name_len = strlen(param_name);

	if (sm->name[name_len] == '-' && /* check for '-' from "->" */
	    strncmp(sm->name, param_name, name_len) == 0) {
		snprintf(buf, sizeof(buf), "$$%s", sm->name + name_len);
		return buf;
	} else if (sm->name[0] == '*' && strcmp(sm->name + 1, param_name) == 0) {
		return "*$$";
	}
	return NULL;
}

static void print_one_return_value_param(int return_id, char *return_ranges,
			int param, struct sm_state *sm, char *implied_rl,
			struct state_list *slist)
{
	const char *param_name;

	param_name = get_param_name(sm);
	if (!param_name)
		return;

	sm_msg("info: return_param_add %d %d '%s' '%s' '%s' %s",
	       return_id, param, return_ranges,
	       param_name, implied_rl, global_static());
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct state_list *my_slist;
	struct sm_state *sm;
	struct smatch_state *extra;
	int param;
	struct range_list *rl;

	my_slist = get_all_states_slist(my_id, slist);

	FOR_EACH_PTR(my_slist, sm) {
		if (!estate_rl(sm->state))
			continue;
		extra = get_state_slist(slist, SMATCH_EXTRA, sm->name, sm->sym);
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
		print_one_return_value_param(return_id, return_ranges, param, sm, show_rl(rl), slist);
	} END_FOR_EACH_PTR(sm);
}

void register_param_set(int id)
{
	if (!option_info)
		return;

	my_id = id;

	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);
	add_returned_state_callback(&print_return_value_param);
}

