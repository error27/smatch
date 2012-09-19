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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(remaining);
STATE(ok);

static void ok_to_use(struct sm_state *sm)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static void match_copy(const char *fn, struct expression *expr, void *unused)
{
	struct expression *call;
	struct expression *arg;
	long long max;

	if (expr->op == SPECIAL_SUB_ASSIGN)
		return;
	set_state_expr(my_id, expr->left, &remaining);

	call = strip_expr(expr->right);
	if (call->type != EXPR_CALL)
		return;
	arg = get_argument_from_call_expr(call->args, 2);
	if (!get_absolute_max(arg, &max))
		max = whole_range.max;
	set_extra_expr_mod(expr->left, alloc_estate_range(0, max));
}

static void match_condition(struct expression *expr)
{
	if (!get_state_expr(my_id, expr))
		return;
	/* If the variable is zero that's ok */
	set_true_false_states_expr(my_id, expr, NULL, &ok);
}

/*
 * This function is biased in favour of print out errors.
 * The heuristic to print is:
 *    If we have a potentially positive return from copy_to_user
 *    and there is a possibility that we return negative as well
 *    then complain.
 */
static void match_return(struct expression *ret_value)
{
	struct smatch_state *state;
	struct sm_state *sm;
	long long min;

	sm = get_sm_state_expr(my_id, ret_value);
	if (!sm)
		return;
	if (!slist_has_state(sm->possible, &remaining))
		return;
	state = get_state_expr(SMATCH_EXTRA, ret_value);
	if (!state)
		return;
	if (!get_absolute_min(ret_value, &min))
		return;
	if (min == 0)
		return;
	sm_msg("warn: maybe return -EFAULT instead of the bytes remaining?");
}

void check_return_efault(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_function_assign_hook_extra("copy_to_user", &match_copy, NULL);
	add_function_assign_hook_extra("__copy_to_user", &match_copy, NULL);
	add_function_assign_hook_extra("copy_from_user", &match_copy, NULL);
	add_function_assign_hook_extra("__copy_from_user", &match_copy, NULL);
	add_function_assign_hook_extra("clear_user", &match_copy, NULL);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_modification_hook(my_id, &ok_to_use);
}
