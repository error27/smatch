/*
 * Copyright (C) 2022 Oracle.
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

STATE(err_ptr);

bool possible_err_ptr(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl;

	state = get_state_expr(SMATCH_EXTRA, expr);
	if (state && estate_is_empty(state))
		return false;

	get_absolute_rl(expr, &rl);
	if (!rl_intersection(rl, alloc_rl(ptr_err_min, ptr_err_max)))
		return false;
	if (!expr_has_possible_state(my_id, expr, &err_ptr))
		return false;
	return true;
}

static void match_return_info(int return_id, char *return_ranges,
				       struct expression *returned_expr,
				       int param,
				       const char *printed_name,
				       struct sm_state *sm)
{
	struct smatch_state *state;

	if (param != -1 || strcmp(printed_name, "$") != 0)
		return;
	if (!slist_has_state(sm->possible, &err_ptr))
		return;

	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!state)
		return;
	if (!rl_intersection(estate_rl(state), alloc_rl(ptr_err_min, ptr_err_max)))
		return;

	sql_insert_return_states(return_id, return_ranges, ERR_PTR, param, printed_name, "");
}

static void match_assign(struct expression *expr)
{
	/*
	 * I felt like I had to implement this function for completeness but
	 * also that it isn't required and might lead to false positives.
	 */
	if (expr->op != '=' || !is_pointer(expr->left))
		return;
	if (!has_states(__get_cur_stree(), my_id))
		return;
	if (possible_err_ptr(expr->right))
		set_state_expr(my_id, expr->left, &err_ptr);
}

static void match_err_ptr(const char *fn, struct expression *expr, void *unused)
{
	set_state_expr(my_id, expr->left, &err_ptr);
}

static void set_error_code(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (macro) {
		if (strcmp(macro, "for_each_gpio_desc") == 0 ||
		    strcmp(macro, "for_each_gpio_desc_with_flag") == 0)
			return;
	}

	set_state(my_id, name, sym, &err_ptr);
}

static void match_is_err_true(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &err_ptr);
}

static void match_is_err_false(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &undefined);
}

void register_kernel_err_ptr(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_modification_hook(my_id, &set_undefined);
	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_return_info_callback(my_id, &match_return_info);

	add_function_assign_hook("ERR_PTR", &match_err_ptr, NULL);
	add_function_assign_hook("ERR_CAST", &match_err_ptr, NULL);
	return_implies_param_key_exact("IS_ERR", int_one, int_one,
					match_is_err_true, 0, "$", NULL);
	return_implies_param_key_exact("IS_ERR", int_zero, int_zero,
					match_is_err_false, 0, "$", NULL);
	return_implies_param_key_exact("PTR_ERR_OR_ZERO", ptr_err_min, ptr_err_max,
					match_is_err_true, 0, "$", NULL);
	return_implies_param_key_exact("PTR_ERR_OR_ZERO", int_zero, int_zero,
					match_is_err_false, 0, "$", NULL);
	return_implies_param_key_exact("IS_ERR_OR_NULL", int_zero, int_zero,
					match_is_err_false, 0, "$", NULL);
	select_return_param_key(ERR_PTR, &set_error_code);
}
