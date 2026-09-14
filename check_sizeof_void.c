/*
 * Copyright 2025 Linaro Ltd.
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

static bool in_asm(void)
{
	struct statement *stmt;

	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	if (!stmt)
		return false;
	if (stmt->type == STMT_ASM)
		return true;
	return false;
}

static bool is_pointer_test(struct expression *expr)
{
	return false;
}

static bool in_is_constexpr(struct expression *expr)
{
	struct string_list *macros;
	char *macro;

	macros = get_all_macros(expr->pos);
	FOR_EACH_PTR(macros, macro) {
		if (!strcmp(macro, "__is_constexpr"))
			return true;
	} END_FOR_EACH_PTR(macro);

	return false;
}

static void match_sizeof(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	// Sparse quirkiness
	type = get_real_base_type(type);
	if (type != &void_ctype)
		return;
	// More Sparse quirkiness
	if (in_asm())
		return;
	if (in_is_constexpr(expr))
		return;
	sm_warning("sizeof(void)");
}

void check_sizeof_void(int id)
{
	my_id = id;

	add_hook(&match_sizeof, SIZEOF_HOOK);
}
