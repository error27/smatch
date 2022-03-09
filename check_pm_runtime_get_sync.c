/*
 * Copyright (C) 2020 Oracle.
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

static unsigned long calls_get_sync;

static void match_condition(struct expression *expr)
{
	struct expression *assigned;

	if (!calls_get_sync)
		return;

	assigned = get_assigned_expr(expr);
	if (!assigned)
		return;
	if (assigned->type != EXPR_CALL)
		return;
	if (!sym_name_is("pm_runtime_get_sync", assigned->fn))
		return;
	sm_warning("pm_runtime_get_sync() also returns 1 on success");
}

static void match_pm_runtime_get_sync(const char *fn, struct expression *expr, void *_arg_nr)
{
	calls_get_sync = 1;
}

void check_pm_runtime_get_sync(int id)
{
	my_id = id;

	add_function_data(&calls_get_sync);

	add_hook(&match_condition, CONDITION_HOOK);
	add_function_assign_hook("pm_runtime_get_sync", &match_pm_runtime_get_sync, INT_PTR(1));
}
