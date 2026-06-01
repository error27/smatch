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

static const char *my_flags;

static void match_function_definition(struct symbol *sym)
{
	struct symbol *arg, *arg_type;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		arg_type = get_base_type(arg);
		if (arg_type && arg_type->ident &&
		    strcmp(arg_type->ident->name, "gfp_t") == 0) {
			my_flags = arg->ident->name;
			return;
		}
	} END_FOR_EACH_PTR(arg);
}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	char *name;

	if (!my_flags)
		return;

	FOR_EACH_PTR(expr->args, arg) {
		name = get_macro_name(arg->pos);
		if (!name || strncmp(name, "GFP_", 4) != 0)
			continue;
		if (strcmp(name, "GFP_KERNEL") != 0)
			continue;
		sm_msg("warn: use '%s' here instead of %s?", my_flags, name);
	} END_FOR_EACH_PTR(arg);
}

void check_not_passing_gfp(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_data((unsigned long *)&my_flags);

	add_hook(&match_function_definition, FUNC_DEF_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}
