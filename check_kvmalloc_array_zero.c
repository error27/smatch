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
 * This check complains when we have a function used with __GFP_ZERO flag and
 * we can use a less verbose alternative.
 *
 * Example: kmalloc_array + __GPF_ZERO = kcalloc
 */

#include "smatch.h"

static int my_id;

struct match_alloc_struct {
	const char *function;
	const char *alternative;
	int flag_pos;
};

struct match_alloc_struct match_alloc_functions[] = {
	{ "kmalloc", "kzalloc", 1 },
	{ "kmalloc_node", "kzalloc_node", 1 },

	{ "kmalloc_array", "kcalloc", 2 },
	{ "kmalloc_array_node", "kcalloc_node", 2 },

	{ "kvmalloc", "kvzalloc", 1 },
	{ "kvmalloc_node", "kvzalloc_node", 1 },

	{ "kvmalloc_array", "kvcalloc", 2 },

	{ "kmem_cache_alloc", "kmem_cache_zalloc", 1 },
	{ NULL, NULL, 0 }
};

static void match_alloc(const char *fn, struct expression *expr, void *_arg)
{
	struct match_alloc_struct *entry = match_alloc_functions;
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
		while (entry->function) {
			if (strcmp(fn, entry->function) == 0) {
				sm_warning("Please consider using %s instead of %s",
					   entry->alternative, entry->function);
				break;
			}
			entry++;
		}
	}
}

void check_kvmalloc_array_zero(int id)
{
	struct match_alloc_struct *entry = match_alloc_functions;

	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	while (entry->function) {
		add_function_hook(entry->function, &match_alloc, INT_PTR(entry->flag_pos));
		entry++;
	}
}
