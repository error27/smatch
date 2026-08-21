/*
 * Copyright (C) 2026 Dan Carpenter
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

static void select_ptracker(const char *name, struct symbol *sym, char *value)
{
	struct range_list *rl = NULL;
	sval_t sval;

	str_to_rl(&ullong_ctype, value, &rl);
	if (rl_to_sval(rl, &sval))
		return;

	set_state(my_id, name, sym, alloc_estate_sval(sval));
}

static void record_merges(struct sm_state *sm)
{
	unsigned long long id;
	struct sm_state *tmp;
	char buf[128];
	int param;

	param = get_param_num_from_sym(sm->sym);
	if (param < 0)
		return;
	if (!sm->sym->ident ||
	    strcmp(sm->sym->ident->name, sm->name) != 0)
		return;

	snprintf(buf, sizeof(buf), "%s %s() %d $",
		is_local(cur_func_sym) ? get_filename() : "extern",
		get_function(), param);
	id = str_to_mtag(buf);

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &merged || tmp->state == &undefined)
			continue;
		sql_insert_ptracker(id, PTRACKER_MERGE, sm->state->name);
	} END_FOR_EACH_PTR(tmp);
}

static void after_def_hook(struct symbol *sym)
{
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state == &merged)
			record_merges(sm);
	} END_FOR_EACH_SM(sm);
}

bool get_old_ptracker(struct expression *expr, sval_t *sval)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (!state || state == &undefined || state == &merged)
		return false;
	*sval = estate_max(state);
	return true;
}

void smatch_ptracker_old(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	select_caller_name_sym(&select_ptracker, PTRACKER);
	add_hook(&after_def_hook, AFTER_DEF_HOOK);
}
