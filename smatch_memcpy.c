/*
 * Copyright (C) 2026 Dan Carpenter.
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

static struct {
	const char *fn, *dest, *src, *size;
} memcpy_fns[] = {
	{ "memset", "$0", NULL, "$2" },
	{ "__memset", "$0", NULL, "$2" },
	{ "__underlying_memset", "$0", NULL, "$2" },
	{ "__builtin_memset", "$0", NULL, "$2" },
	{ "memcpy", "$0", "$1", "$2" },
	{ "memmove", "$0", "$1", "$2" },
	{ "__memcpy", "$0", "$1", "$2" },
	{ "__builtin_memcpy", "$0", "$1", "$2" },
	{ "__builtin_memmove", "$0", "$1", "$2" },
	{ "copy_from_user", "$0", NULL, "$2" },
	{ "memcpy_fromio", "$0", NULL, "$2" },
	{ "copy_to_user", NULL, "$1", "$2" },
};

static struct expr3_fn_list *memcpy_hooks;

void add_memcpy_hook(expr3_func *fn)
{
	add_ptr_list(&memcpy_hooks, fn);
}

static struct expression *get_copy_expr(struct expression *call,
					const char *key)
{
	if (!key)
		return NULL;
	return gen_expr_from_dollar_key(call, key);
}

static void match_memcpy(const char *fn, struct expression *expr, void *data)
{
	typeof(*memcpy_fns) *info = data;
	struct expression *dest, *src, *size;

	dest = get_copy_expr(expr, info->dest);
	src = get_copy_expr(expr, info->src);
	size = get_copy_expr(expr, info->size);
	call_expr3_fns(memcpy_hooks, dest, src, size);
}

void smatch_memcpy(int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(memcpy_fns); i++)
		add_function_hook(memcpy_fns[i].fn, &match_memcpy, &memcpy_fns[i]);
}
