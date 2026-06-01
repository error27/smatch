/*
 * Copyright (C) 2013 Oracle.
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

static int my_id;

STATE(devm);

static void match_assign(const char *fn, struct expression *expr, void *unused)
{
	set_state_expr(my_id, expr->left, &devm);
}

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	/*
	 * Only handle devm_ memory allocator
	 */
	if (strncmp(info->fn_name, "devm_", 5) == 0)
		match_assign(name, expr, NULL);
}

static void match_reassign(struct expression *expr)
{
	struct expression *left, *right;

	if (expr->op != '=')
		return;

	right = strip_expr(expr->right);
	if (!get_state_expr(my_id, right))
		return;

	left = strip_expr(expr->left);

	set_state_expr(my_id, left, &devm);
}

struct state_list *get_devm_variables(void)
{
	struct state_list *slist = NULL;
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		// FIXME check sm->state

		add_ptr_list(&slist, sm);
	} END_FOR_EACH_SM(sm);

	return slist;
}

bool is_devm_pointer(struct expression *expr)
{
	if (get_state_expr(my_id, expr) == &devm)
		return true;
	return false;
}

void smatch_kernel_devm(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_allocation_hook(&match_allocation);

	// FIXME modification hook

	/* These should really be handled by smatch_allocation.c */
	add_function_assign_hook("devm_kstrdup", &match_assign, NULL);
	add_function_assign_hook("devm_kasprintf", &match_assign, NULL);
	add_function_assign_hook("devm_kvasprintf", &match_assign, NULL);


	add_hook(&match_reassign, ASSIGNMENT_HOOK);
}
