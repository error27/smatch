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

/*
 * This checks complains when we have kvmalloc_array used with
 * __GFP_ZERO flag, we can instead use kvcalloc which is designed
 * for it.
 * Similarly  kmalloc_array + __GPF_ZERO = kcalloc.
 */

#include "smatch.h"

static int my_id;

static void match_alloc(const char *fn, struct expression *expr, void *_arg)
{
	unsigned long gfp;
	int arg_nr = PTR_INT(_arg);
	struct expression *arg_expr;
	sval_t sval;

	arg_expr = get_argument_from_call_expr(expr->args, arg_nr);
	if (!get_value(arg_expr, &sval))
		return;	
	
	if (!macro_to_ul("__GFP_ZERO", &gfp))
		return;

	if (sval.uvalue & gfp) {
		if (strcmp(fn,"kvmalloc_array") == 0)
			sm_warning("Please consider using kvcalloc instead");
		else
			sm_warning("Please consider using kcalloc instead");
	}
}

void check_kvmalloc_array_zero(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_hook("kvmalloc_array", &match_alloc, INT_PTR(2));
	add_function_hook("kmalloc_array", &match_alloc, INT_PTR(2));
}

