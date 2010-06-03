/*
 * smatch/check_return_efault.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * This tries to find places which should probably return -EFAULT
 * but return the number of bytes to copy instead.
 */

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

STATE(remaining);
STATE(reassigned);

static void ok_to_use(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(my_id, name, sym, &reassigned);
}

static void match_copy(const char *fn, struct expression *expr, void *unused)
{
	if (expr->op == SPECIAL_SUB_ASSIGN)
		return;
	set_state_expr(my_id, expr->left, &remaining);
}

static void match_return(struct expression *ret_value)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, ret_value);
	if (!state || state != &remaining)
		return;
	state = get_state_expr(SMATCH_EXTRA, ret_value);
	if (!state)
		return;
	if (possibly_true(SPECIAL_EQUAL, get_dinfo(state), 0, 0))
		return;
	sm_msg("warn: maybe return -EFAULT instead of the bytes remaining?");
}

void check_return_efault(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_function_assign_hook("copy_to_user", &match_copy, NULL);
	add_function_assign_hook("__copy_to_user", &match_copy, NULL);
	add_function_assign_hook("copy_from_user", &match_copy, NULL);
	add_function_assign_hook("__copy_from_user", &match_copy, NULL);
	add_function_assign_hook("clear_user", &match_copy, NULL);
	add_hook(&match_return, RETURN_HOOK);
	set_default_modification_hook(my_id, ok_to_use);
}
