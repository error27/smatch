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

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static void match_netdev_priv(const char *fn, struct expression *expr, void *unused)
{

	struct expression *right, *arg;

	right = strip_expr(expr->right);
	if (right->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(right->args, 0);
	arg = strip_expr(arg);
	if (!arg || arg->type != EXPR_SYMBOL)
		return;

	set_state_expr(my_id, arg, alloc_state_expr(expr->left));
}

struct expression *get_netdev_priv(struct expression *dev)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, dev);
	if (!state)
		return NULL;
	return state->data;
}

void register_kernel_netdev_priv(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	set_dynamic_states(my_id);
	add_function_assign_hook("netdev_priv", &match_netdev_priv, NULL);
}
