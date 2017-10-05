/*
 * Copyright (C) 2015 Oracle.
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
#include "smatch_slist.h"

static int my_id;

struct stree *used_stree;
STATE(used);

/*
 * I am a loser.  This should be a proper hook, but I am too lazy.  I'll add
 * that stuff if we have a second user.
 */
void __get_state_hook(int owner, const char *name, struct symbol *sym)
{
	int arg;

	if (!option_info)
		return;
	if (__in_fake_assign)
		return;

	arg = get_param_num_from_sym(sym);
	if (arg < 0)
		return;

	set_state_stree(&used_stree, my_id, name, sym, &used);
}

static void set_param_used(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;
	int arg_nr;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	arg_nr = get_param_num_from_sym(sym);
	if (arg_nr < 0)
		goto free;

	set_state(my_id, name, sym, &used);
free:
	free_string(name);
}

static void process_states(struct stree *stree)
{
	struct sm_state *tmp;
	int arg;
	const char *name;

	FOR_EACH_SM(used_stree, tmp) {
		arg = get_param_num_from_sym(tmp->sym);
		name = get_param_name(tmp);
		if (!name)
			continue;
		sql_insert_call_implies(PARAM_USED, arg, name, "1");
	} END_FOR_EACH_SM(tmp);

	free_stree(&used_stree);
}

void register_param_used(int id)
{
	my_id = id;

	if (!option_info)
		return;

	select_call_implies_hook(PARAM_USED, &set_param_used);
	all_return_states_hook(&process_states);
}
