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

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

STATE(impossible);

int is_impossible_path(void)
{
	if (get_state(my_id, "impossible", NULL) == &impossible)
		return 1;
	return 0;
}

static void handle_compare(struct expression *left, int op, struct expression *right)
{
	if (!possibly_true(left, op, right))
		set_true_false_states(my_id, "impossible", NULL, &impossible, NULL);
	if (!possibly_false(left, op, right))
		set_true_false_states(my_id, "impossible", NULL, NULL, &impossible);

}

static void match_condition(struct expression *expr)
{
	if (expr->type == EXPR_COMPARE)
		handle_compare(expr->left, expr->op, expr->right);
	else
		handle_compare(expr, SPECIAL_NOTEQUAL, zero_expr());
}

void register_impossible(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
}


