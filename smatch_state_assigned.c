/*
 * Copyright (C) 2022 Oracle.
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
#include "smatch_extra.h"

static int my_id;
static int ssa_id;

static sm_hook **hooks;
static sm_hook **ssa_hooks;

void add_state_assigned_hook(int owner, sm_hook *call_back)
{
	if (hooks[owner])
		sm_fatal("multiple state_assigned_hook hooks for %s", check_name(owner));
	hooks[owner] = call_back;
}

void add_ssa_state_assigned_hook(int owner, sm_hook *call_back)
{
	if (ssa_hooks[owner])
		sm_fatal("multiple ssa_state_assigned_hook hooks for %s", check_name(owner));
	ssa_hooks[owner] = call_back;
}

static void call_ssa_hooks(struct sm_state *ssa_sm, struct sm_state *sm, struct expression *expr)
{
	struct sm_state *tmp;

	if (!ssa_sm)
		return;
	if (!ssa_hooks[sm->owner])
		return;
	/* should be safe to assume sm->sym is NULL */
	if (sm->sym)
		return;

	/*
	 * Merged ssa states are a bit complicated to handle.
	 * This is the laziest way to handle it, but it works for my
	 * only use case and it might even be a good way to handle it.
	 */
	FOR_EACH_PTR(ssa_sm->possible, tmp) {
		if (tmp->state == &merged ||
		    tmp->state == &undefined)
			continue;
		if (strcmp(tmp->state->name, sm->name) == 0)
			ssa_hooks[sm->owner](sm, expr);
	} END_FOR_EACH_PTR(tmp);

}

static void match_assignment(struct expression *expr)
{
	struct expression *right;
	struct sm_state *ssa_sm, *sm;
	struct symbol *sym;
	char *name;

	right = strip_expr(expr->right);
	name = expr_to_var_sym(right, &sym);
	if (!name || !sym)
		return;

	ssa_sm = get_sm_state(ssa_id, name, sym);

	FOR_EACH_SM(__get_cur_stree(), sm) {
		if (sm->owner >= num_checks)
			continue;

		call_ssa_hooks(ssa_sm, sm, expr);

		if (hooks[sm->owner] &&
		    sm->sym == sym &&
		    strcmp(sm->name, name) == 0)
			hooks[sm->owner](sm, expr);
	} END_FOR_EACH_SM(sm);

	free_string(name);
}

void register_state_assigned(int id)
{
	my_id = id;

	hooks = malloc(num_checks * sizeof(*hooks));
	memset(hooks, 0, num_checks * sizeof(*hooks));

	ssa_id = id_from_name("register_ssa");
	ssa_hooks = malloc(num_checks * sizeof(*ssa_hooks));
	memset(ssa_hooks, 0, num_checks * sizeof(*ssa_hooks));

	add_hook(&match_assignment, ASSIGNMENT_HOOK_AFTER);
}

