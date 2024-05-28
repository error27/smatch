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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static struct expression *warned;

static void match_likely(const char *fn, struct expression *expr, void *unused)
{
	struct expression *parent;

	if (expr == warned)
		return;

	parent = expr_get_parent_expr(expr);
	if (!parent || parent->type != EXPR_COMPARE)
		return;
	warned = expr;
	sm_warning("check likely/unlikely parentheses");
}

void check_unlikely_parens(int id)
{
	my_id = id;

	add_function_hook("__builtin_expect", &match_likely, NULL);
}
