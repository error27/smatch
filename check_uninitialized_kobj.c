/*
 * Copyright (C) 2023 Oracle.
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
#include "smatch_extra.h"

static int my_id;

static void match_kobject_function(struct expression *expr, const char *name,
				   struct symbol *sym, void *data)
{
	struct sm_state *sm, *tmp;

	if (db_incomplete())
		return;

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!sm)
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (rl_max(estate_rl(tmp->state)).value == 0)
			sm_warning("Calling kobject_put|get with state->initialized unset from line: %d",
				   tmp->line);
	} END_FOR_EACH_PTR(tmp);
}

void check_uninitialized_kobj(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_param_key_hook("kobject_put", &match_kobject_function, 0,
				    "$->state_initialized", NULL);
	add_function_param_key_hook("kobject_get", &match_kobject_function, 0,
				    "$->state_initialized", NULL);
}
