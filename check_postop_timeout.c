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

STATE(timed_out);

static void match_while_count_down(struct expression *expr)
{
	struct statement *stmt = __cur_stmt;

	if (expr->type != EXPR_POSTOP || expr->op != SPECIAL_DECREMENT)
		return;

	if (!stmt || stmt->type != STMT_ITERATOR)
		return;

	set_true_false_states_expr(my_id, strip_expr(expr->unop), NULL, &timed_out);
}

static void match_test(struct expression *expr)
{
	struct sm_state *sm;
	char *name;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm || !slist_has_state(sm->possible, &timed_out))
		return;

	name = expr_to_str(expr);
	sm_msg("warn: should this be '%s == -1'", name);
	free_string(name);
}

void check_postop_timeout(int id)
{
	my_id = id;
	add_hook(&match_while_count_down, CONDITION_HOOK);
	add_hook(&match_test, CONDITION_HOOK);
	add_modification_hook(my_id, &set_undefined);
}
