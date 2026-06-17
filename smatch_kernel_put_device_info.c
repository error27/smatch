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

static int my_id;

#define IGNORE 100000

STATE(put);
STATE(ignore);

static struct function_return_info func_table[] = {
	{ "put_device", PUT_DEVICE, 0, "$" },
	{ "get_device", IGNORE, 0, "$" },
	{ "get_device", IGNORE, -1, "$" },
	{ "free_netdev", IGNORE, 0, "&$->dev" }, /* handled by smatch_free.c */
	{}
};

static struct name_sym_fn_list *put_device_hooks;

void add_put_device_hook(name_sym_hook *hook)
{
	add_ptr_list(&put_device_hooks, hook);
}

void set_ignore_put_device(const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &ignore);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (sm->state == &put &&
	    parent_is_gone_var_sym(sm->name, sm->sym))
		return &put;
	return &undefined;
}

static bool refcount_bumped(const char *name, struct symbol *sym)
{
	static int refcount_id;
	struct smatch_state *state;
	char ref[64];

	if (!refcount_id)
		refcount_id = id_from_name("check_refcount_info");

	if (name[0] == '&')
		snprintf(ref, sizeof(ref), "%s.kobj.kref.refcount.refs.counter", name);
	else
		snprintf(ref, sizeof(ref), "%s->kobj.kref.refcount.refs.counter", name);

	state = get_state(refcount_id, ref, sym);
	if (state && strcmp(state->name, "inc") == 0)
		return true;
	return false;
}

static void match_put_device(struct expression *expr,
		const char *name, struct symbol *sym,
		const char *value, void *data)
{
	if (!data && in_function_table(expr, func_table))
		return;

	if (get_state(my_id, name, sym) || refcount_bumped(name, sym)) {
		set_state(my_id, name, sym, &ignore);
		return;
	}
	call_name_sym_fns(put_device_hooks, expr, name, sym);
	set_state(my_id, name, sym, &put);
}

static void return_param_ignore(struct expression *expr,
		const char *name, struct symbol *sym,
		const char *value, void *data)
{
	set_state(my_id, name, sym, &ignore);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	if (param < 0 || sm->state != &put)
		return;
	sql_insert_return_states(return_id, return_ranges, PUT_DEVICE,
				 param, printed_name, "");
}

void smatch_kernel_put_device_info(int id)
{
	struct type_handler_pair hooks[] = {
		{ PUT_DEVICE, match_put_device },
		{ IGNORE, return_param_ignore},
		{}
	};

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	load_function_table(func_table, hooks);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_return_info_callback(my_id, return_info_callback);
	select_return_param_key(PUT_DEVICE, &match_put_device);
}

