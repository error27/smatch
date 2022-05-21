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
int __ignore_param_used;

static struct stree *used_stree;

STATE(used);

static void get_state_hook(int owner, const char *name, struct symbol *sym)
{
	static const char *prev;
	int arg;

	if (!option_info)
		return;

	if (__ignore_param_used ||
	    __in_fake_assign ||
	    __in_fake_parameter_assign ||
	    __in_function_def ||
	    __in_unmatched_hook)
		return;

	if (!name || name[0] == '&')
		return;

	if (name == prev)
		return;
	prev = name;

	arg = get_param_num_from_sym(sym);
	if (arg < 0)
		return;
	if (param_was_set_var_sym(name, sym))
		return;
	if (parent_was_PARAM_CLEAR(name, sym))
		return;

	set_state_stree(&used_stree, my_id, name, sym, &used);
}

static void set_param_used(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;
	int arg_nr;

	if (!option_info)
		return;

	if (key[0] == '&')
		return;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	arg_nr = get_param_num_from_sym(sym);
	if (arg_nr < 0)
		goto free;
	if (param_was_set_var_sym(name, sym))
		goto free;
	set_state_stree(&used_stree, my_id, name, sym, &used);
free:
	free_string(name);
}

static void process_states(void)
{
	struct sm_state *tmp;
	int arg;
	const char *name;

	FOR_EACH_SM(used_stree, tmp) {
		arg = get_param_num_from_sym(tmp->sym);
		if (arg < 0)
			continue;
		name = get_param_name(tmp);
		if (!name)
			continue;
		if (strcmp(name, "$") == 0 ||
		    strcmp(name, "*$") == 0)
			continue;
		if (is_recursive_member(name))
			continue;

		if (is_ignored_kernel_data(name))
			continue;

		sql_insert_return_implies(PARAM_USED, arg, name, "");
	} END_FOR_EACH_SM(tmp);

	free_stree(&used_stree);
}

static void match_function_def(struct symbol *sym)
{
	free_stree(&used_stree);
}

void register_param_used(int id)
{
	my_id = id;

	add_hook(&match_function_def, AFTER_FUNC_HOOK);
	add_function_data((unsigned long *)&used_stree);

	add_get_state_hook(&get_state_hook);

	select_return_implies_hook(PARAM_USED, &set_param_used);
	all_return_states_hook(&process_states);
}
