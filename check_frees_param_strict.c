/*
 * Copyright (C) 2014 Oracle.
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
 * This file is sort of like check_dereferences_param.c.  In theory the one
 * difference should be that the param is NULL it should still be counted as a
 * free.  But for now I don't handle that case.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(ignore);

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (parent_is_null_var_sym(sm->name, sm->sym))
		return sm->state;
	return &undefined;
}

static void set_ignore(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->sym && sm->sym->ident &&
	    strcmp(sm->sym->ident->name, sm->name) == 0)
		return;
	set_state(my_id, sm->name, sm->sym, &ignore);
}

void track_freed_param(struct expression *expr, struct smatch_state *state)
{
	struct expression *tmp;
	int cnt = 0;

	while ((tmp = get_assigned_expr(expr))) {
		expr = strip_expr(tmp);
		if (cnt++ > 5)
			break;
	}

	if (get_param_num(expr) < 0)
		return;
	if (param_was_set(expr))
		return;

	set_state_expr(my_id, expr, state);
}

void track_freed_param_var_sym(const char *name, struct symbol *sym,
			       struct smatch_state *state)
{
	if (get_param_num_from_sym(sym) < 0)
		return;
	/*
	 * FIXME: some unpublished code made this apply to only '$' and I
	 * don't remember why I would do that.  But there was probably
	 * a reason.
	 */
	if (param_was_set_var_sym(name, sym))
		return;

	set_state(my_id, name, sym, state);
}

static void param_freed_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	int param;
	const char *param_name;

	if (on_atomic_dec_path())
		return;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (strcmp(sm->state->name, "freed") != 0)
//		if (!slist_has_state(sm->possible, &freed))
			continue;

		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;

		param_name = get_param_name(sm);
		if (!param_name)
			continue;

		sql_insert_return_states(return_id, return_ranges, PARAM_FREED,
					 param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

void check_frees_param_strict(int id)
{
	my_id = id;

	add_modification_hook(my_id, &set_ignore);
	add_split_return_callback(&param_freed_info);
	add_unmatched_state_hook(my_id, &unmatched_state);
}
