/*
 * Copyright (C) 2026 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "smatch.h"

static int my_id;

STATE(terminated);

static void match_nul_terminate(struct expression *expr)
{
	set_state_expr(my_id, expr, &terminated);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr, int param,
				 const char *printed_name, struct sm_state *sm)
{
	if (!slist_has_state(sm->possible, &terminated))
		return;

	sql_insert_return_states(return_id, return_ranges,
				 ADDS_TERMINATOR, param, printed_name, "");
}

void smatch_terminator_xfun(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_nul_terminate_callback(&match_nul_terminate);
	add_return_info_callback(my_id, &return_info_callback);
}
