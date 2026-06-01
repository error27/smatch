/*
 * Copyright (C) 2026 Dan Carpenter
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

bool in_function_table(struct expression *expr, struct function_return_info *table)
{
	int i;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return false;

	if (expr->fn->type != EXPR_SYMBOL)
		return false;

	for (i = 0; table[i].name; i++) {
		if (sym_name_is(expr->fn, table[i].name))
			return true;
	}

	return false;
}

static param_key_hook *get_hook(int type, struct type_handler_pair *hooks)
{
	int i;

	for (i = 0; hooks[0].hook; i++) {
		if (type == hooks[i].type)
			return hooks[i].hook;
	}
	return NULL;
}

void load_function_table(struct function_return_info *table, struct type_handler_pair *hooks)
{
	struct function_return_info *info;
	int i;

	for (i = 0; table[i].name; i++) {
		param_key_hook *hook;

		info = &table[i];
		hook = get_hook(info->type, hooks);
		if (!hook)
			exit(1);

		if (info->call_back) {
			add_function_hook(info->name, info->call_back, info);
		} else if (info->implies_start && info->type == ALLOC) {
			return_implies_param_key(info->name,
					*info->implies_start,
					*info->implies_end,
					hook, info->param, info->key, info);
		} else if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start,
					*info->implies_end,
					hook, info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name,
				hook, info->param, info->key, info);
		}
	}
}
