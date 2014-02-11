/*
 * Copyright (C) 2012 Oracle.
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

static void match_condition(struct expression *expr)
{
	sval_t val;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;

	if (!get_value(expr->right, &val))
		return;
	sm_msg("warn: was '== %s' instead of '='", sval_to_str(val));
}

void check_assign_vs_compare(int id)
{
	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
}
