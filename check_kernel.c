/*
 * sparse/check_kernel.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * This is kernel specific stuff for smatch_extra.
 */

#include "smatch.h"
#include "smatch_extra.h"

static int implied_copy_return(struct expression *call, void *unused, struct range_list **rl)
{
	struct expression *arg;
	sval_t max;

	arg = get_argument_from_call_expr(call->args, 2);
	get_absolute_max(arg, &max);
	*rl = alloc_rl(ll_to_sval(0), max);
	return 1;
}

static void match_param_nonnull(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *_param)
{
	int param = PTR_INT(_param);
	struct expression *arg;
	struct smatch_state *pre_state;
	struct smatch_state *true_state;

	arg = get_argument_from_call_expr(call_expr->args, param);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	true_state = estate_filter_sval(pre_state, ll_to_sval(0));
	set_extra_expr_nomod(arg, true_state);
}

static void match_container_of(const char *fn, struct expression *expr, void *unused)
{
	set_extra_expr_mod(expr->left, alloc_estate_range(valid_ptr_min_sval, valid_ptr_max_sval));
}

void check_kernel(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	return_implies_state("tomoyo_memory_ok", 1, 1, &match_param_nonnull, (void *)0);
	add_macro_assign_hook_extra("container_of", &match_container_of, NULL);

	add_implied_return_hook("copy_to_user", &implied_copy_return, NULL);
	add_implied_return_hook("__copy_to_user", &implied_copy_return, NULL);
	add_implied_return_hook("copy_from_user", &implied_copy_return, NULL);
	add_implied_return_hook("__copy_fom_user", &implied_copy_return, NULL);
	add_implied_return_hook("clear_user", &implied_copy_return, NULL);
}
