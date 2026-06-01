/*
 * Copyright 2025 Linaro Ltd.
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

STATE(err_ptr);

bool reasonable_err_ptr(struct expression *expr)
{
	return expr_has_possible_state(my_id, expr, &err_ptr);
}

static struct expression *ignore_mod;
static inline void clear_state(struct sm_state *sm, struct expression *mod_expr)
{
	if (mod_expr && mod_expr->type == EXPR_ASSIGNMENT &&
	    mod_expr->right == ignore_mod)
		return;
	set_state(sm->owner, sm->name, sm->sym, &undefined);
}

static void match_returns_error_pointers(struct expression *call, struct expression *arg, char *key, char *value)
{
	if (strcmp(key, "$") != 0)
		return;
	ignore_mod = call;
	set_state_expr(my_id, arg, &err_ptr);
}

void smatch_kernel_err_ptr_possible(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	select_return_implies_hook(ERR_PTR, &match_returns_error_pointers);
	add_modification_hook(my_id, &clear_state);
}
