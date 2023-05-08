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
 * This is an --info recipe.  The goal is to print a message for every parameter
 * which we can not avoid dereferencing.  This is maybe a bit restrictive but it
 * avoids some false positives.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(derefed);

static void process_states(void)
{
	struct sm_state *tmp;
	int arg;
	const char *name;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		if (tmp->state != &derefed)
			continue;
		arg = get_param_num_from_sym(tmp->sym);
		if (arg < 0)
			continue;
		name = get_param_name(tmp);
		if (!name || name[0] == '&')
			continue;
		sql_insert_return_implies(DEREFERENCE, arg, name, "1");
	} END_FOR_EACH_SM(tmp);
}

static void deref_hook(struct expression *expr)
{
	char *param_name = NULL;
	struct symbol *sym, *param_sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return;

	param_name = get_param_var_sym_var_sym(name, sym, NULL, &param_sym);
	if (!param_name || !param_sym)
		goto free;
	if (get_param_num_from_sym(param_sym) < 0)
		goto free;
	if (param_was_set_var_sym(param_name, param_sym))
		goto free;

	set_state(my_id, param_name, param_sym, &derefed);
free:
	free_string(name);
}

void check_dereferences_param(int id)
{
	my_id = id;

	add_dereference_hook(deref_hook);
	all_return_states_hook(&process_states);
}
