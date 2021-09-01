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

static int warn_line;
static void schedule(void)
{
	if (is_impossible_path())
		return;
	if (get_preempt_cnt() <= 0)
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

