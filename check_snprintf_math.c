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

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(snprintf_ret);

static bool is_snprintf_call(struct expression *expr)
{
	if (!expr || expr->type != EXPR_CALL)
		return false;
	return sym_name_is(expr->fn, "snprintf");
}

static void match_assign(struct expression *expr)
{
	if (is_snprintf_call(expr->right) ||
	    expr_has_possible_state(my_id, expr->right, &snprintf_ret))
		set_state_expr(my_id, expr->left, &snprintf_ret);
}

static void match_binop(struct expression *expr)
{
	if (expr->op != '-')
		return;
	if (!expr_has_possible_state(my_id, expr->left, &snprintf_ret))
		return;

	sm_msg("use scnprintf() instead of snprintf()");
}

void check_snprintf_math(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_binop, BINOP_HOOK);
	add_modification_hook(my_id, &set_undefined);
}
