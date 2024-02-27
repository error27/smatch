/*
 * Copyright 2024 Linaro Ltd.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static void match_xa_is_err_true(const char *fn, struct expression *call_expr,
				 struct expression *assign_expr, void *unused)
{
	struct expression *arg;
	struct smatch_state *pre_state;
	struct range_list *rl;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	rl = estate_rl(pre_state);
	if (!rl)
		rl = alloc_rl(ptr_xa_err_min, ptr_xa_err_max);
	rl = rl_intersection(rl, alloc_rl(ptr_xa_err_min, ptr_xa_err_max));
	rl = cast_rl(get_type(arg), rl);
	set_extra_expr_nomod(arg, alloc_estate_rl(rl));
}

static void match_xa_is_err_false(const char *fn, struct expression *call_expr,
				  struct expression *assign_expr, void *unused)
{
	struct expression *arg;
	struct smatch_state *pre_state;
	struct range_list *rl;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	pre_state = get_state_expr(SMATCH_EXTRA, arg);
	if (pre_state) {
		rl = estate_rl(pre_state);
		rl = remove_range(rl, ptr_xa_err_min, ptr_xa_err_max);
	} else {
		rl = alloc_rl(valid_ptr_min_sval, valid_ptr_max_sval);
	}
	rl = cast_rl(get_type(arg), rl);
	set_extra_expr_nomod(arg, alloc_estate_rl(rl));
}

static int implied_xa_err_return(struct expression *call, void *unused, struct range_list **rl)
{
	struct expression *arg;
	struct range_list *param_rl;

	arg = get_argument_from_call_expr(call->args, 0);
	if (!get_implied_rl(arg, &param_rl))
		return false;
	if (sval_cmp(rl_min(param_rl), ptr_xa_err_min) >= 0 &&
	    sval_cmp(rl_max(param_rl), ptr_xa_err_max) <= 0) {
		*rl = alloc_rl(err_min, err_max);
		return true;
	}

	return false;
}

void register_kernel_xa_err(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	return_implies_state("xa_is_err", 1, 1, &match_xa_is_err_true, NULL);
	return_implies_state("xa_is_err", 0, 0, &match_xa_is_err_false, NULL);

	add_implied_return_hook("xa_err", &implied_xa_err_return, NULL);
}
