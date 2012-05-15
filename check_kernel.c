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

static void match_err_cast(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	struct expression *right;
	struct range_list *rl;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, 0);

	if (get_implied_range_list(arg, &rl))
		set_extra_expr_mod(expr->left, alloc_estate_range_list(rl));
	else
		set_extra_expr_mod(expr->left, alloc_estate_range(-4095, -1));
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
	true_state = add_filter(pre_state, 0);
	set_extra_expr_nomod(arg, true_state);
}

static void match_container_of(const char *fn, struct expression *expr, void *unused)
{
	set_extra_expr_mod(expr->left, alloc_estate_range(1, POINTER_MAX));
}

void check_kernel(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	add_function_assign_hook("ERR_PTR", &match_err_cast, NULL);
	add_function_assign_hook("ERR_CAST", &match_err_cast, NULL);
	add_function_assign_hook("PTR_ERR", &match_err_cast, NULL);
	return_implies_state("IS_ERR_OR_NULL", 0, 0, &match_param_nonnull, (void *)0);
	return_implies_state("tomoyo_memory_ok", 1, 1, &match_param_nonnull, (void *)0);
	add_macro_assign_hook("container_of", &match_container_of, NULL);
}
