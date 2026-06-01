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

static const char *get_free_fn_name(struct expression *expr)
{
	if (expr->type != EXPR_CALL)
		return NULL;
	return get_fn_name(expr->fn);
}

static void match_free_member(struct expression *expr, const char *name, struct symbol *sym, bool maybe)
{
	struct expression *arg;
	struct symbol *type;
	const char *fn_name;
	char *type_str;
	char *member;

	if (__in_fake_assign)
		return;

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_DEREF || !arg->member)
		return;

	type = get_type(arg->deref);
	if (!type || !type->ident)
		return;

	type_str = type_to_str(type);
	if (!type_str)
		return;

	member = get_member_name_no_prefix(arg);
	if (!member)
		return;

	fn_name = get_free_fn_name(expr);
	if (!fn_name)
		return;

	if (maybe)
		sql_insert_function_type_info(MAYBE_FREED, type_str, member, fn_name);
	else
		sql_insert_function_type_info(FREED, type_str, member, fn_name);
}

static void match_free(struct expression *expr, const char *name, struct symbol *sym)
{
	match_free_member(expr, name, sym, false);
}

static void match_maybe_free(struct expression *expr, const char *name, struct symbol *sym)
{
	match_free_member(expr, name, sym, true);
}

void smatch_free_locations(int id)
{
	add_free_hook(&match_free);
	add_maybe_free_hook(&match_maybe_free);
}
