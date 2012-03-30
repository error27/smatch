/*
 * smatch/check_return_enomem.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
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

static void ok_to_use(const char *name, struct symbol *sym, struct expression *expr,
		void *unused)
{
	set_state(my_id, name, sym, &ok);
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
	long long val;

	if (!ret_value)
		return;
	if (returns_unsigned(cur_func_sym))
		return;
	if (returns_pointer(cur_func_sym))
		return;
	if (!get_value(ret_value, &val) || val != -1)
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
	return_implies_state("kmalloc", 1, POINTER_MAX, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kmalloc", 0, 0, &allocation_failed, INT_PTR(0));
	return_implies_state("kzalloc", 1, POINTER_MAX, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kzalloc", 0, 0, &allocation_failed, INT_PTR(0));
	return_implies_state("kcalloc", 1, POINTER_MAX, &allocation_succeeded, INT_PTR(0));
	return_implies_state("kcalloc", 0, 0, &allocation_failed, INT_PTR(0));
	add_hook(&match_return, RETURN_HOOK);
	set_default_modification_hook(my_id, ok_to_use);
}
