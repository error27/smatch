/*
 * Copyright (C) 2026 Oracle.
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

static int my_id;

bool get_user_len(struct expression *expr, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (!state || estate_is_empty(state))
		return false;

	*rl = estate_rl(state);
	return true;
}

static struct smatch_state *unmatched_empty(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static void set_empty(struct sm_state *sm, struct expression *mod_expr)
{
	struct allocation_info info = {};
	struct range_list *rl;

	if (mod_expr && mod_expr->type == EXPR_ASSIGNMENT &&
	    mod_expr->op == '=' &&
	    load_allocation_info(mod_expr->right, &info) &&
	    get_user_rl(info.total_size, &rl)) {
		set_state(sm->owner, sm->name, sm->sym, alloc_estate_rl(rl));
		return;
	}
	set_state(sm->owner, sm->name, sm->sym, alloc_estate_empty());
}

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	struct range_list *rl;

	if (!expr || expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;
	if (!info->total_size)
		return;
	if (!get_user_rl(info->total_size, &rl))
		return;

	set_state_expr(my_id, expr->left, alloc_estate_rl(rl));
}

static void match_nla_data_size(const char *fn, struct expression *expr,
				void *unused)
{
	struct range_list *rl;

	/* FIXME: Handle nla_data() buffers with unknown sizes. */
	if (!get_nl_data_size(expr->right, &rl))
		return;

	set_state_expr(my_id, expr->left, alloc_estate_rl(rl));
}

static void match_assign(struct expression *expr)
{
	struct smatch_state *state;

	if (expr->op != '=')
		return;
	state = get_state_expr(my_id, expr->right);
	if (!state)
		return;

	set_state_expr(my_id, expr->left, state);
}

static void insert_caller_info(struct expression *call, int param,
				char *printed_name, struct sm_state *sm)
{
	sval_t sval;

	if (estate_is_empty(sm->state))
		return;
	if (estate_get_single_value(sm->state, &sval) && sval.value == 0)
		return;

	sql_insert_caller_info(call, USER_LEN, param, printed_name,
			       sm->state->name);
}

static void select_caller_info(const char *name, struct symbol *sym,
				char *key, char *value)
{
	struct range_list *rl = NULL;
	char fullname[256];

	if (strncmp(key, "$", 1) != 0)
		return;

	snprintf(fullname, sizeof(fullname), "%s%s", name, key + 1);
	str_to_rl(&ulong_ctype, value, &rl);
	if (!rl)
		return;
	set_state(my_id, fullname, sym, alloc_estate_rl(rl));
}

void smatch_user_len(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_allocation_hook(&match_allocation);
	add_unmatched_state_hook(my_id, &unmatched_empty);
	add_merge_hook(my_id, &merge_estates);
	add_modification_hook(my_id, &set_empty);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	if (option_project == PROJ_KERNEL)
		add_function_assign_hook("nla_data", &match_nla_data_size, NULL);
	add_caller_info_callback(my_id, &insert_caller_info);
	select_caller_info_hook(&select_caller_info, USER_LEN);
}
