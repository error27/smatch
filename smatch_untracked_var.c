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

/*
 * This just tracks local variables where we have saved the address to them.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(untracked);

static void match_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (!right || right->type != EXPR_PREOP || right->op != '&')
		return;
	right = right->unop;
	if (!right || right->type != EXPR_SYMBOL)
		return;
	set_state_expr(my_id, right, &untracked);
}

bool is_untracked(struct expression *expr)
{
	if (get_state_expr(my_id, expr))
		return true;
	return false;
}

void register_untracked_var(int id)
{
	my_id = id;

	add_hook(match_assign, ASSIGNMENT_HOOK);
}
