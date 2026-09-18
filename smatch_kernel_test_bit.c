/*
 * Copyright (C) Dan Carpenter
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

static struct bit_fn {
	const char *fn;
	int bit, bitmap;
} fn_list[] = {
        { "arch___clear_bit", 0, 1},
        { "arch___set_bit", 0, 1},
        { "clear_bit", 0, 1},
        { "__clear_bit_le", 0, 1},
        { "clear_bit_le", 0, 1},
        { "cpumask_test_cpu", 0, 1},
        { "cpu_online", 0, 1},
        { "generic_test_bit", 0, 1},
        { "__set_bit", 0, 1},
        { "set_bit", 0, 1},
        { "__set_bit_le", 0, 1},
        { "set_bit_le", 0, 1},
        { "test_and_clear_bit", 0, 1},
        { "__test_and_clear_bit_le", 0, 1},
        { "test_and_clear_bit_le", 0, 1},
        { "___test_and_set_bit", 0, 1},
        { "__test_and_set_bit", 0, 1},
        { "test_and_set_bit", 0, 1},
        { "__test_and_set_bit_le", 0, 1},
        { "test_and_set_bit_le", 0, 1},
        { "_test_bit", 0, 1},
        { "test_bit", 0, 1},
        { "test_bit_le", 0, 1},
        { "variable_test_bit", 0, 1},
};

static struct expr3_fn_list *test_bit_hooks;

void add_test_bit_hook(expr3_func *fn)
{
	add_ptr_list(&test_bit_hooks, fn);
}

static void match_test_bit(const char *fn, struct expression *expr, void *_info)
{
	struct bit_fn *info = _info;
	struct expression *bit, *bitmap;

	bit = get_argument_from_call_expr(expr->args, info->bit);
	bitmap = get_argument_from_call_expr(expr->args, info->bitmap);

	bit = strip_expr(bit);
	bitmap = strip_expr(bitmap);

	call_expr3_fns(test_bit_hooks, expr, bit, bitmap);
}

void smatch_kernel_test_bit(int id)
{
	struct bit_fn *info;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	for (i = 0; i < ARRAY_SIZE(fn_list); i++) {
		info = &fn_list[i];
		add_function_hook(info->fn, &match_test_bit, info);
	}
}
