/*
 * Copyright (C) 2010 Dan Carpenter.
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
 * Complains about places that return -1 instead of -ENOMEM
 */

#include "smatch.h"

static int my_id;

static struct expression *get_assigned_ptr(struct expression *expr)
{
	if (!expr)
		return NULL;
	if (expr->type == EXPR_COMPARE &&
	    expr->op == SPECIAL_EQUAL &&
	    expr_is_zero(expr->right))
		return get_assigned_ptr(expr->left);

	return get_assigned_expr(expr);
}

static void match_return(struct expression *expr)
{
	struct expression *cond, *assign;
	struct statement *stmt;
	sval_t sval;

	if (!expr)
		return;
	if (returns_unsigned(cur_func_sym))
		return;
	if (returns_pointer(cur_func_sym))
		return;
	if (!get_value(expr, &sval))
		return;
	if (sval.value == -12 || sval.value == 0 || !sval_is_negative(sval))
		return;
	if (get_macro_name(expr->pos))
		return;

	stmt = get_parent_if_stmt(expr);
	if (!stmt)
		return;
	cond = strip_expr(stmt->if_conditional);
	assign = get_assigned_ptr(cond);
	if (!is_allocation_primitive(assign))
		return;
	sm_warning("return -ENOMEM on allocation failure");
}

void check_return_enomem(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
}
