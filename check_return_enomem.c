/*
 * Copyright (C) 2010 Dan Carpenter.
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
 * Complains about places that return -1 instead of -ENOMEM
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

#define ENOMEM 12

static int my_id;

STATE(enomem);
STATE(ok);

static void ok_to_use(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static void allocation_succeeded(const char *fn, struct expression *call_expr,
				struct expression *assign_expr, void *unused)
{
	set_state_expr(my_id, assign_expr->left, &ok);
}

static void allocation_failed(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *_arg_no)
{
	set_state_expr(my_id, assign_expr->left, &enomem);
}

static void match_return(struct expression *ret_value)
{
	struct sm_state *sm;
	struct state_list *slist;
	sval_t sval;

	if (!ret_value)
		return;
	if (returns_unsigned(cur_func_sym))
		return;
	if (returns_pointer(cur_func_sym))
		return;
	if (!get_value(ret_value, &sval) || sval.value != -1)
		return;
	if (get_macro_name(ret_value->pos))
		return;

	slist = get_all_states(my_id);

	FOR_EACH_PTR(slist, sm) {
		if (sm->state == &enomem) {
			sm_msg("warn: returning -1 instead of -ENOMEM is sloppy");
			goto out;
		}
	} END_FOR_EACH_PTR(sm);

out:
	free_slist(&slist);
}

void check_return_enomem(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	return_implies_state("kmalloc", valid_ptr_min, valid_ptr_max, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kmalloc", 0, 0, &allocation_failed, INT_PTR(0));
	return_implies_state("kzalloc", valid_ptr_min, valid_ptr_max, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kzalloc", 0, 0, &allocation_failed, INT_PTR(0));
	return_implies_state("kcalloc", valid_ptr_min, valid_ptr_max, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kcalloc", 0, 0, &allocation_failed, INT_PTR(0));
	add_hook(&match_return, RETURN_HOOK);
	add_modification_hook(my_id, &ok_to_use);
}
