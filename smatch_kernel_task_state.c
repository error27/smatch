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

STATE(not_running);

bool task_not_running(void)
{
	if (has_possible_state(my_id, "task_state", NULL, &not_running))
		return true;
	return false;
}

static void state_change_hook(struct expression *expr, const char *str)
{
	if (strcmp(str, "not_running") == 0)
		set_state(my_id, "task_state", NULL, &not_running);
	else
		set_state(my_id, "task_state", NULL, &undefined);
}

static void called_not_running(const char *name, struct symbol *sym, char *key, char *value)
{
	set_state(my_id, "task_state", NULL, &not_running);
}

static void match_call_info(struct expression *expr)
{
	if (task_not_running())
		sql_insert_caller_info(expr, TASK_NOT_RUNNING, -2, "", "");
}

void register_kernel_task_state(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_set_current_state_hook(state_change_hook);
	select_caller_info_hook(&called_not_running, TASK_NOT_RUNNING);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
}
