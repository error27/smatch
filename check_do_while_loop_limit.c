/*
 * Copyright (C) 2021 Oracle.
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

STATE(post_minus);

static bool is_post_minus(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (expr->type != EXPR_POSTOP)
		return false;
	if (expr->op != SPECIAL_DECREMENT)
		return false;
	return true;
}

static void match_post_loop(struct expression *expr)
{
	struct statement *stmt;

	expr = strip_expr(expr);
	stmt = expr_get_parent_stmt(expr);

	if (!stmt)
		return;
	if (stmt->type != STMT_ITERATOR)
		return;
	if (!is_post_minus(stmt->iterator_post_condition))
		return;
	if (is_post_minus(stmt->iterator_post_condition))
		set_state_expr(my_id, stmt->iterator_post_condition->left, &post_minus);
}

static void match_condition(struct expression *expr)
{
	if (get_state_expr(my_id, expr) != &post_minus)
		return;
	sm_warning("do while ends on '%s == -1'", expr_to_str(expr));
}

void check_do_while_loop_limit(int id)
{
	my_id = id;
	add_hook(&match_post_loop, CONDITION_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
