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
 * This tries to find places which should probably return -EFAULT
 * but return the number of bytes to copy instead.
 */

#include <string.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static bool is_sscanf(struct expression *expr)
{
	if (!expr || expr->type != EXPR_CALL)
		return false;
	return sym_name_is("sscanf", expr->fn);
}

static void match_return(struct expression *expr)
{
	struct sm_state *sm, *tmp;

	sm = get_assigned_sm(expr);
	if (!sm)
		return;
	FOR_EACH_PTR(sm->possible, tmp) {
		if (is_sscanf(tmp->state->data)) {
			sm_warning("sscanf doesn't return error codes");
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

void check_sscanf_return(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
}
