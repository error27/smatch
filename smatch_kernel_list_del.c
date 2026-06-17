/*
 * Copyright 2023 Linaro Ltd.
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

STATE(deleted);

struct func_info {
	const char *name;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct func_info list_del_table[] = {
	{ "devm_led_classdev_register", 0, "&$->node" },
	{ "list_del", 0, "$" },
	{ "list_del_init", 0, "$" },
};

static struct name_sym_fn_list *del_hooks;

void add_list_del_hook(name_sym_hook *fn)
{
	add_ptr_list(&del_hooks, fn);
}

static void list_del(struct expression *expr,
		const char *name, struct symbol *sym,
		const char *value, void *data)
{
	set_state(my_id, name, sym, &deleted);
	call_name_sym_fns(del_hooks, expr, name, sym);
}

static void list_add(struct expression *expr, const char *name, struct symbol *sym)
{
	if (!get_state(my_id, name, sym))
		return;
	set_state(my_id, name, sym, &undefined);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	/*
	 * probably this check is not even required.  We could just say
	 * list_del if the state exists at all.
	 */
	if (!slist_has_state(sm->possible, &deleted))
		return;
	sql_insert_return_states(return_id, return_ranges,
				 LIST_DEL,
				 param, printed_name, "");
}

void smatch_kernel_list_del(int id)
{
	struct func_info *info;
	int i;

	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	for (i = 0; i < ARRAY_SIZE(list_del_table); i++) {
		info = &list_del_table[i];

		if (info->call_back) {
			add_function_hook(info->name, info->call_back, info);
		} else if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start,
					*info->implies_end,
					&list_del,
					info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name,
				&list_del, info->param, info->key, info);
		}
	}

	add_return_info_callback(my_id, return_info_callback);
	select_return_param_key(LIST_DEL, &list_del);
	add_list_add_entry_hook(&list_add);
}
