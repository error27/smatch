/*
 * Copyright (C) 2019 Oracle.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(suspicious);

static void check_param(struct expression *expr)
{
	struct expression *tmp;
	sval_t sval;

	tmp = get_assigned_expr(expr);
	if (tmp)
		expr = tmp;
	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_PREOP || expr->op != '&')
		return;
	if (!get_implied_value(expr, &sval) ||
	    sval.value == 0 || sval.uvalue > 4096)
		return;

	expr = strip_expr(expr->unop);
	while (expr->type == EXPR_DEREF) {
		expr = strip_expr(expr->deref);
		if (expr->type == EXPR_PREOP && expr->op == '*')
			expr = strip_expr(expr->unop);
		if (get_implied_value(expr, &sval) && sval.value == 0) {
			set_state_expr(my_id, expr, &suspicious);
			return;
		}
	}
}

static void match_call(struct expression *expr)
{
	struct expression *arg;

	FOR_EACH_PTR(expr->args, arg) {
		check_param(arg);
	} END_FOR_EACH_PTR(arg);
}

static void check_variable(struct sm_state *sm)
{
	struct sm_state *extra_sm, *tmp;
	int line = sm->line;

	extra_sm = get_sm_state(SMATCH_EXTRA, sm->name, sm->sym);
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
			sm_warning_line(line, "address of NULL pointer '%s'",
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

void check_bogus_address_param(int id)
{
	my_id = id;

	add_hook(&match_call, FUNCTION_CALL_HOOK);
	all_return_states_hook(&process_states);
}
