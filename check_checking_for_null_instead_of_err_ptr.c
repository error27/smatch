/*
 * Copyright (C) 2017 Oracle.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static const char *untrusted_fn_ptrs[] = {
	"(struct target_core_fabric_ops)->fabric_make_np",
	"(struct target_core_fabric_ops)->fabric_make_tpg",
	"(struct configfs_group_operations)->make_item",
	"(cgroup_subsys_state)->css_alloc",
	/*
	 * debugfs stuff should not be checked but checking for NULL is harmless
	 * and some people see the error message and convert it to an IS_ERR()
	 * check which is buggy.  So these warnings introduce a risk.
	 *
	 */
	"debugfs_create_dir",
	"debugfs_create_file",
};

static bool from_untrusted_fn_ptr(struct expression *expr)
{
	struct expression *prev;
	bool ret = false;
	char *fn;
	int i;

	prev = get_assigned_expr(expr);
	if (!prev || prev->type != EXPR_CALL)
		return false;

	fn = get_fnptr_name(prev->fn);
	if (!fn)
		return false;

	for (i = 0; i < ARRAY_SIZE(untrusted_fn_ptrs); i++) {
		if (strcmp(fn, untrusted_fn_ptrs[i]) == 0) {
			ret = true;
			break;
		}
	}
	free_string(fn);
	return ret;
}

static void match_condition(struct expression *expr)
{
	char *name;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->left);

	if (!is_pointer(expr))
		return;

	if (implied_not_equal(expr, 0) &&
	    possible_err_ptr(expr) &&
	    !from_untrusted_fn_ptr(expr)) {
		name = expr_to_str(expr);
		sm_msg("warn: '%s' is an error pointer or valid", name);
		free_string(name);
	}
}

static void match_condition2(struct expression *expr)
{
	struct range_list *rl;
	struct data_range *drange;
	char *name;

	if (!is_pointer(expr))
		return;
	if (!get_implied_rl(expr, &rl))
		return;

	FOR_EACH_PTR(rl, drange) {
		if (sval_cmp(drange->min, drange->max) != 0)
			continue;
		if (drange->min.value >= -4095 && drange->min.value < 0)
			goto warn;
	} END_FOR_EACH_PTR(drange);

	return;

warn:
	if (from_untrusted_fn_ptr(expr))
		return;

	name = expr_to_str(expr);
	sm_warning("'%s' could be an error pointer", name);
	free_string(name);
}

void check_checking_for_null_instead_of_err_ptr(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
	if (option_spammy)
		add_hook(&match_condition2, CONDITION_HOOK);
}

