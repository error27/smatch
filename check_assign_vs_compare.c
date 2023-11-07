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

static bool is_false_positive(struct expression *expr)
{
	char *macro;

	if (option_project != PROJ_KERNEL)
		return false;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return false;

	if (strcmp(macro, "btree_err_on") == 0)
		return true;
	if (strcmp(macro, "btree_err") == 0)
		return true;

	return false;
}

static void check_constant(struct expression *expr)
{
	sval_t val;

	if (!get_value(expr->right, &val))
		return;
	if (is_false_positive(expr))
		return;
	sm_warning("was '== %s' instead of '='", sval_to_str(val));
}

static void check_address(struct expression *expr)
{
	char *str;
	struct expression *right = strip_expr(expr->right);

	if (!__cur_stmt || __cur_stmt->type != STMT_IF)
		return;

	if (right->type != EXPR_PREOP ||
	    right->op != '&')
		return;

	if (get_macro_name(expr->pos))
		return;

	str = expr_to_str(right);
	sm_warning("was '== %s' instead of '='", str);
	free_string(str);
}

static void match_condition(struct expression *expr)
{
	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;

	check_constant(expr);
	check_address(expr);
}

void check_assign_vs_compare(int id)
{
	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
}
