/*
 * Copyright (C) 2022 Dan Carpenter.
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

static void match_condition1(struct expression *expr)
{
	char *left, *right;

	if (expr->type != EXPR_BINOP || expr->op != '/')
		return;

	if (get_macro_name(expr->pos))
		return;

	left = expr_to_str(expr->left);
	right = expr_to_str(expr->right);
	sm_warning("replace divide condition '%s / %s' with '%s >= %s'",
		   left, right, left, right);
	free_string(left);
	free_string(right);
}

void check_divide_condition(int id)
{
	my_id = id;

	add_hook(&match_condition1, CONDITION_HOOK);
}
