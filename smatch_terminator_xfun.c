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

static void save_terminators(int return_id, char *return_ranges,
			     struct expression *returned_expr)
{
	struct sm_state *sm;
	const char *param_name;
	int param;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state != &terminated)
			continue;

		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;
		param_name = get_param_name(sm);
		if (!param_name)
			continue;

		sql_insert_return_states(return_id, return_ranges,
					 ADDS_TERMINATOR, param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

void smatch_terminator_xfun(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_nul_terminate_callback(&match_nul_terminate);
	add_split_return_callback(&save_terminators);
}
