/*
 * Copyright (C) 2010 Dan Carpenter.
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
 * This is like check_deref_check.c except that it complains about code like:
 * if (a)
 *        a->foo = 42;
 * a->bar = 7;
 *
 * Of course, Smatch has complained about these for forever but the problem is
 * the old scripts were too messy and complicated and generated too many false
 * positives.
 *
 * This check is supposed to be simpler because it only looks for one kind of 
 * null dereference bug instead of every kind.  It also gets rid of the false 
 * positives caused by the checks that happen inside macros.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(null);
STATE(ok);

static void is_ok(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &ok);
}

static void check_dereference(struct expression *expr)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	expr = strip_expr(expr);
	if (getting_address())
		return;
	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return;
	if (is_ignored(my_id, sm->name, sm->sym))
		return;
	if (implied_not_equal(expr, 0))
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &merged)
			continue;
		if (tmp->state == &ok)
			continue;
		if (tmp->state == &null) {
			sm_msg("error: we previously assumed '%s' could be null (see line %d)",
			       tmp->name, tmp->line);
			add_ignore(my_id, sm->name, sm->sym);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

static void match_dereferences(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	check_dereference(expr->unop);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	check_dereference(expr->unop->left);
}

static void set_param_dereferenced(struct expression *arg, char *unused)
{
	check_dereference(arg);
}

static void match_condition(struct expression *expr)
{
	if (get_macro_name(expr->pos))
		return;

	if (expr->type == EXPR_ASSIGNMENT) {
		match_condition(expr->right);
		match_condition(expr->left);
	}
	set_true_false_states_expr(my_id, expr, &ok, &null);
}

void check_check_deref(int id)
{
	my_id = id;

	add_modification_hook(my_id, &is_ok);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_call_implies_hook(DEREFERENCE, &set_param_dereferenced);
	add_hook(&match_condition, CONDITION_HOOK);
}
