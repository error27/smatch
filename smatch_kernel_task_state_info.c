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

STATE(running);
STATE(not_running);

static unsigned long complicated;

struct state_param {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct state_param func_table[] = {
	{ "prepare_to_wait_event", 0, 2, "$", &int_zero, &int_zero},
	{ "finish_wait", TASK_RUNNING },
};

static struct string_hook_list *hooks;
void add_set_current_state_hook(string_hook *hook)
{
	add_ptr_list(&hooks, hook);
}

void set_task_state(struct expression *expr, struct smatch_state *state)
{
	call_string_hooks(hooks, expr, state->name);
	if (get_state(my_id, "task_state", NULL))
		complicated = true;
	set_state(my_id, "task_state", NULL, state);
}

static void do_set_current_state(struct expression *expr)
{
	sval_t sval;

	if (get_implied_value(expr, &sval)) {
		if (sval.value == 0)
			set_task_state(expr, &running);
		else
			set_task_state(expr, &not_running);
	} else {
		set_task_state(expr, &undefined);
	}
}

static void match_declaration(struct symbol *sym)
{
	char *name;

	/* The set_current_state() macro is a pain in the butt */
	if (!sym->initializer)
		return;
	if (!sym->ident || strcmp(sym->ident->name, "__ret") != 0)
		return;

	name = expr_to_var(sym->initializer);
	if (!name)
		return;
	if (strcmp(name, "state") == 0)
		do_set_current_state(sym->initializer);
	free_string(name);
}

static void select_task_running(struct expression *expr, int param, char *key, char *value)
{
	set_task_state(expr, &running);
}

static void select_task_not_running(struct expression *expr, int param, char *key, char *value)
{
	set_task_state(expr, &not_running);
}

static void match_running(const char *fn, struct expression *expr, void *_info)
{
	set_task_state(expr, &running);
}

static void match_not_running(const char *fn, struct expression *expr, void *_info)
{
	set_task_state(expr, &not_running);
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *state;

	if (complicated)
		return;

	state = get_state(my_id, "task_state", NULL);
	if (state != &running && state != &not_running)
		return;

	sql_insert_return_states(return_id, return_ranges,
			(state == &running) ? TASK_RUNNING : TASK_NOT_RUNNING,
			-2, "", "");
}

void register_kernel_task_state_info(int id)
{
	struct state_param *info;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&complicated);
	add_hook(&match_declaration, DECLARATION_HOOK);

	add_split_return_callback(&match_return_info);
	select_return_states_hook(TASK_RUNNING, &select_task_running);
	select_return_states_hook(TASK_NOT_RUNNING, &select_task_not_running);

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];
		if (info->type == TASK_RUNNING)
			add_function_hook(info->name, &match_running, info);
		else if (info->type == TASK_NOT_RUNNING)
			add_function_hook(info->name, &match_not_running, info);
		else
			return_implies_param_key_expr(info->name,
						*info->implies_start, *info->implies_end,
						&do_set_current_state,
						info->param, info->key, info);
	}
}
