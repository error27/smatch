/*
 * Copyright (C) 2026 Dan Carpenter.
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

static void check_reachable(struct expression *expr)
{
	struct statement *outer;
	struct statement *stmt;

	if (__inline_fn)
		return;
	if (!__path_is_null())
		return;

	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	if (!stmt)
		return;
	if (stmt->type != STMT_GOTO && stmt->type != STMT_RETURN)
		return;
	outer = get_parent_stmt(expr);
	if (outer && in_macro(outer->pos))
		return;

	sm_warning_line(expr->pos.line, "replace while loop with if statement?");
}

void check_pointless_loop(int id)
{
	my_id = id;
	add_hook(check_reachable, PRE_LOOP_CONDITION_TWO);
}
