/*
 * smatch/check_input_free_device.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Don't call input_free_device() after calling
 * input_unregister_device()
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

STATE(no_free);
STATE(ok);

static int my_id;

static void match_assign(struct expression *expr)
{
	if (get_state_expr(my_id, expr->left)) {
		set_state_expr(my_id, expr->left, &ok);
	}
}

static void match_input_unregister(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, 0);
	set_state_expr(my_id, arg, &no_free);
}

static void match_input_free(const char *fn, struct expression *expr, void *data)
{
	struct expression *arg;
	struct sm_state *sm;

	arg = get_argument_from_call_expr(expr->args, 0);
	sm = get_sm_state_expr(my_id, arg);
	if (!sm)
		return;
	if (!slist_has_state(sm->possible, &no_free))
		return;
	sm_msg("error: don't call input_free_device() after input_unregister_device()");
}

void check_input_free_device(int id)
{
	my_id = id;
	if (option_project != PROJ_KERNEL)
		return;
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_function_hook("input_unregister_device", &match_input_unregister, NULL);
	add_function_hook("input_free_device", &match_input_free, NULL);
}
