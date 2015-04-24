/*
 * Copyright (C) 2015 Oracle.
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
#include "smatch_extra.h"

static bool is_non_null_array(struct expression *expr)
{
	struct symbol *type;
	struct symbol *sym;
	struct symbol *tmp;
	char *name;
	int i;

	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return 0;
	if (expr->type == EXPR_SYMBOL)
		return 1;
	if (implied_not_equal(expr, 0))
		return 1;

	/* verify that it's not the first member of the struct */
	if (expr->type != EXPR_DEREF || !expr->member)
		return 0;
	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!name || !sym)
		return 0;
	type = get_real_base_type(sym);
	if (!type || type->type != SYM_PTR)
		return 0;
	type = get_real_base_type(type);
	if (type->type != SYM_STRUCT)
		return 0;

	i = 0;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		i++;
		if (!tmp->ident)
			continue;
		if (strcmp(expr->member->name, tmp->ident->name) == 0) {
			if (i == 1)
				return 0;
			return 1;
		}
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

int get_address_rl(struct expression *expr, struct range_list **rl)
{
	if (is_non_null_array(expr)) {
		*rl = alloc_rl(array_min_sval, array_max_sval);
		return 1;
	}

	return 0;
}
