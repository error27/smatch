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
#include "smatch_slist.h"

static int my_id;

STATE(added);

struct func_info {
	const char *name;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct func_info list_add_table[] = {
	{ "list_add_tail", 0, "$" },
	{ "list_add", 0, "$" },
	{ "prepare_to_wait", 1, "&$->entry" },
	{ "prepare_to_wait_event", 1, "&$->entry", &int_zero, &int_zero },
};

static struct name_sym_fn_list *add_hooks;
void add_list_add_entry_hook(name_sym_hook *fn)
{
	add_ptr_list(&add_hooks, fn);
}

static void param_added(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &added);
	call_name_sym_fns(add_hooks, expr, name, sym);
}

static void list_del(struct expression *expr, const char *name, struct symbol *sym)
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
//	if (!slist_has_state(sm->possible, &added))
//		return;
	if (sm->state != &added)
		return;
	sql_insert_return_states(return_id, return_ranges,
				 LIST_ADD_ENTRY,
				 param, printed_name, "");
}

int get_list_add_line(const char *var, struct symbol *sym)
{
	struct sm_state *sm;
	const char *p;
	int len = 0;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->sym != sym)
			continue;
		if (!slist_has_state(sm->possible, &added))
			continue;
		p = strstr(sm->name, var);
		if (!p)
			continue;
		if (!len)
			len = strlen(var);
		if (p[len] != '-' &&
		    p[len] != '.')
			continue;
		return sm->line;
	} END_FOR_EACH_SM(sm);

	return -1;
}

void smatch_kernel_list_add_entry(int id)
{
	struct func_info *info;
	int i;

	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	for (i = 0; i < ARRAY_SIZE(list_add_table); i++) {
		info = &list_add_table[i];

		if (info->call_back) {
			add_function_hook(info->name, info->call_back, info);
		} else if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start,
					*info->implies_end,
					&param_added,
					info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name,
				&param_added, info->param, info->key, info);
		}
	}

	add_return_info_callback(my_id, return_info_callback);
	select_return_param_key(LIST_ADD_ENTRY, &param_added);
	add_list_del_hook(&list_del);
}
