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

static void match_param_nonnull(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *_param)
{
	int param = (int)_param;
	struct expression *arg;
	struct smatch_state *pre_state;
	struct smatch_state *true_state;

	arg = get_argument_from_call_expr(call_expr->args, param);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	true_state = add_filter(pre_state, 0);
	set_extra_expr_nomod(arg, true_state);
}

void check_kernel(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	return_implies_state("IS_ERR_OR_NULL", 0, 0, &match_param_nonnull, (void *)0);
	return_implies_state("tomoyo_memory_ok", 1, 1, &match_param_nonnull, (void *)0);
}
