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
#include "smatch_function_hashtable.h"

static int my_id;

DEFINE_STRING_HASHTABLE_STATIC(unconstant_macros);

int is_unconstant_macro(struct expression *expr)
{
	char *ident;

	ident = get_macro_name(expr->pos);
	if (!ident)
		ident = pos_ident(expr->pos);
	if (!ident)
		return 0;
	if (search_unconstant_macros(unconstant_macros, ident))
		return 1;
	return 0;
}

void register_unconstant_macros(int id)
{
	my_id = id;

	unconstant_macros = create_function_hashtable(100);
	load_strings("unconstant_macros", unconstant_macros);
}
