/*
 * Copyright (C) 2018 Oracle.
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

static bool is_strange_GFP_function(struct expression *expr)
{
	char *name;
	bool ret = false;

	if (!expr || expr->type != EXPR_CALL)
		return false;

	name = expr_to_str(expr->fn);
	if (!name)
		return false;

	if (strncmp(name, "__xa_", 5) == 0 ||
	    strncmp(name, "xa_", 3) == 0 ||
	    strcmp(name, "ttm_bo_swapout") == 0 ||
	    strcmp(name, "mas_store_gfp") == 0)
		ret = true;

	free_string(name);
	return ret;
}

static int warn_line;
static void schedule(struct expression *expr)
{
	if (is_impossible_path())
		return;
	if (get_preempt_cnt() <= 0)
		return;

	if (is_strange_GFP_function(expr))
		return;

	if (warn_line == get_lineno())
		return;
	warn_line = get_lineno();

	sm_msg("warn: sleeping in atomic context");
}

void check_scheduling_in_atomic(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_sleep_callback(&schedule);
}

