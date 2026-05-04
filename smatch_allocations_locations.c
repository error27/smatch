/*
 * Copyright (C) 2026 Oracle.
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

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	struct expression *left;
	struct symbol *type;
	char *type_str;
	char *member;

	if (__in_fake_assign)
		return;

	if (!expr || expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;

	left = strip_expr(expr->left);
	if (!left || left->type != EXPR_DEREF || !left->member)
		return;

	type = get_type(left->deref);
	if (!type || !type->ident)
		return;

	type_str = type_to_str(type);
	if (!type_str)
		return;

	member = get_member_name_no_prefix(left);
	if (!member)
		return;

	sql_insert_function_type_info(ALLOC, type_str, member, info->fn_name);
}

void smatch_allocations_locations(int id)
{
	add_allocation_hook(&match_allocation);
}
