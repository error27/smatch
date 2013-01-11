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

static int implied_err_cast_return(struct expression *call, void *unused, struct range_list **rl)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(call->args, 0);
	if (!get_implied_rl(arg, rl))
		*rl = alloc_rl(ll_to_sval(-4095), ll_to_sval(-1));
	return 1;
}

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
	true_state = add_filter(pre_state, ll_to_sval(0));
	set_extra_expr_nomod(arg, true_state);
}

static void match_not_err(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *unused)
{
	struct expression *arg;
	struct smatch_state *pre_state;
	struct smatch_state *new_state;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	new_state = filter_range(pre_state, sval_type_min(&long_ctype), ll_to_sval(-1));
	set_extra_expr_nomod(arg, new_state);
}

static void match_err(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *unused)
{
	struct expression *arg;
	struct smatch_state *pre_state;
	struct smatch_state *new_state;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	new_state = filter_range(pre_state, sval_type_min(&long_ctype), ll_to_sval(-4096));
	new_state = filter_range(new_state, ll_to_sval(0), sval_type_max(&long_ctype));
	set_extra_expr_nomod(arg, new_state);
}

static void match_container_of(const char *fn, struct expression *expr, void *unused)
{
	set_extra_expr_mod(expr->left, alloc_estate_range(valid_ptr_min_sval, valid_ptr_max_sval));
}

void check_kernel(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	add_implied_return_hook("ERR_PTR", &implied_err_cast_return, NULL);
	add_implied_return_hook("ERR_CAST", &implied_err_cast_return, NULL);
	add_implied_return_hook("PTR_ERR", &implied_err_cast_return, NULL);
	return_implies_state("IS_ERR_OR_NULL", 0, 0, &match_param_nonnull, (void *)0);
	return_implies_state("IS_ERR", 0, 0, &match_not_err, NULL);
	return_implies_state("IS_ERR", 1, 1, &match_err, NULL);
	return_implies_state("tomoyo_memory_ok", 1, 1, &match_param_nonnull, (void *)0);
	add_macro_assign_hook_extra("container_of", &match_container_of, NULL);

	add_implied_return_hook("copy_to_user", &implied_copy_return, NULL);
	add_implied_return_hook("__copy_to_user", &implied_copy_return, NULL);
	add_implied_return_hook("copy_from_user", &implied_copy_return, NULL);
	add_implied_return_hook("__copy_fom_user", &implied_copy_return, NULL);
	add_implied_return_hook("clear_user", &implied_copy_return, NULL);
}
