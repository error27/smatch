/*
 * Copyright (C) 2021 Oracle.
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

/*
 * The kernel uses gotos a lot for unwinding.  So it's useful to be able to
 * track all the gotos so we can check error codes are set etc.
 *
 */

#include "smatch.h"

static int my_id;

static void match_goto(struct statement *stmt)
{
	if (stmt->type != STMT_GOTO)
		return;
	set_state(my_id, "goto", NULL, &true_state);
}

struct sm_state *get_goto_sm_state(void)
{
	return get_sm_state(my_id, "goto", NULL);
}

void register_goto_tracker(int id)
{
	my_id = id;

	add_hook(&match_goto, STMT_HOOK);
}
