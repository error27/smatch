/*
 * Copyright 2023 Linaro Ltd.
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

STATE(suspicious);

static void deref_hook(struct expression *expr)
{
	struct expression *tmp;
	sval_t sval;

	/*
	 * This is kind of weird.  You would think that if you had a
	 * "foo = 0;" assignment, then foo would be zero, but there is
	 * some kind of weirdness with how Smatch fakes alloc_netdev_mqs()
	 * handling so that's not always true.
	 *
	 */

	if (!get_implied_value(expr, &sval) || sval.value != 0)
		return;

	tmp = get_assigned_expr(expr);
	if (!tmp || !expr_is_zero(tmp))
		return;
	if (is_impossible_path())
		return;

	set_state_expr(my_id, expr, &suspicious);
}

static void check_variable(struct sm_state *sm)
{
	struct sm_state *extra_sm, *tmp;
	int line = sm->line;

	extra_sm = get_extra_sm_name_sym(sm->name, sm->sym);
	if (!extra_sm)
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &suspicious)
			line = tmp->line;
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(extra_sm->possible, tmp) {
		if (!estate_rl(tmp->state))
			continue;
		if (rl_min(estate_rl(tmp->state)).value != 0) {
			sm_warning_line(line,
				"pointer dereferenced without being set '%s'",
				sm->name);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

static void process_states(void)
{
	struct sm_state *tmp;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		check_variable(tmp);
	} END_FOR_EACH_SM(tmp);
}

void check_deref_before_set(int id)
{
	my_id = id;

	add_dereference_hook(deref_hook);
	all_return_states_hook(&process_states);
}
