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

/*
 * Once you've used an fd one time, you can't use it again.
 * But the problem is that an fd is just an int.  It might be nice if we
 * tracked this in smatch_units.c.  Another hacky thing (not nice) might
 * be to mark it as freed after use so it could show up as a use after
 * free.  But instead I am doing this.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(fget);

static void match_fget(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	if (local_debug) {
		struct sm_state *sm = get_sm_state(my_id, name, sym);
		sm_msg("%s: expr='%s' name='%s' sm='%s'", __func__, expr_to_str(expr), name, show_sm(sm));
	}
	if (has_possible_state(my_id, name, sym, &fget))
		sm_warning("double fget(): '%s'", name);
	set_state(my_id, name, sym, &fget);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	sql_insert_return_states(return_id, return_ranges,
				 FGET,
				 param, printed_name, "");
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (strcmp(printed_name, "$") != 0)
		return;
	sm_warning("fd re-used after fget(): '%s'", sm->name);
}

void check_double_fget(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_param_key_hook("fget", &match_fget, 0, "$", NULL);
	add_function_param_key_hook("fdget", &match_fget, 0, "$", NULL);
	add_return_info_callback(my_id, return_info_callback);
	select_return_param_key(FGET, &match_fget);

	add_caller_info_callback(my_id, caller_info_callback);
}
