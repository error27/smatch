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

static int uses_whole_data(struct expression *call, int size)
{
	struct expression *arg;
	sval_t sval;

	FOR_EACH_PTR(call->args, arg) {
		if (get_implied_value(arg, &sval) && sval_cmp_val(sval, size) == 0)
			return 1;
	} END_FOR_EACH_PTR(arg);
	return 0;
}

static void match_check_params(struct expression *call)
{
	struct expression *arg;
	struct expression *param;
	struct symbol *cast_type, *type;
	char *name;
	sval_t sval;

	FOR_EACH_PTR(call->args, arg) {
		if (arg->type != EXPR_CAST)
			continue;
		param = strip_expr(arg);
		if (param->type != EXPR_PREOP || param->op != '&')
			continue;
		cast_type = get_pointer_type(arg);
		if (!cast_type || cast_type->type != SYM_BASETYPE ||
		    type_bits(cast_type) == 8)
			continue;
		type = get_pointer_type(param);
		if (!type || type->type != SYM_BASETYPE)
			continue;
		if (types_equiv(cast_type, type))
			continue;
		if (!get_implied_value(param->unop, &sval) || sval.value == 0)
			continue;
		if (uses_whole_data(call, type_bytes(type)))
			continue;
		name = expr_to_var_sym(param->unop, NULL);
		sm_msg("warn: does endianness matter for '%s'?", name);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

void check_endian_cast(int id)
{
	my_id = id;

	add_hook(&match_check_params, FUNCTION_CALL_HOOK);
}
