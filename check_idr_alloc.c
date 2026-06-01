/*
 * Copyright (C) 2026 Oracle
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

#include "parse.h"
#include "smatch.h"

static int my_id;

static void match_idr_alloc(struct expression *expr)
{
	struct range_list *rl;

	if (!get_user_rl(expr, &rl))
		return;

	rl = cast_rl(&int_ctype, rl);
	if (!sval_is_negative(rl_min(rl)))
		return;

	sm_warning("idr_alloc start value from user can be < 0");
}

void check_idr_alloc(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_param_key_expr_hook("idr_alloc", &match_idr_alloc, 2, "$", NULL);
}
