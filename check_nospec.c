/*
 * Copyright (C) 2018 Oracle.
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

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(nospec);

bool is_nospec(struct expression *expr)
{
	char *macro;

	if (get_state_expr(my_id, expr) == &nospec)
		return true;
	macro = get_macro_name(expr->pos);
	if (macro && strcmp(macro, "array_index_nospec") == 0)
		return true;
	return false;
}

static void nospec_assign(struct expression *expr)
{
	if (is_nospec(expr->right))
		set_state_expr(my_id, expr->left, &nospec);
}

static void set_param_nospec(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 1);
	else
		return;

	set_state(my_id, fullname, sym, &nospec);
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	int i = 0;

	FOR_EACH_PTR(expr->args, arg) {
		if (get_state_expr(my_id, arg) == &nospec)
			sql_insert_caller_info(expr, NOSPEC, i, "$", "");
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	sql_insert_caller_info(call, NOSPEC, param, printed_name, "");
}

static void returned_struct_members(int return_id, char *return_ranges, struct expression *expr)
{
	struct symbol *returned_sym;
	struct sm_state *sm;
	const char *param_name;
	int param;

	returned_sym = expr_to_sym(expr);

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		param = get_param_num_from_sym(sm->sym);
		if (param < 0) {
			if (!returned_sym || returned_sym != sm->sym)
				continue;
			param = -1;
		}

		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		if (param != -1 && strcmp(param_name, "$") == 0)
			continue;

		sql_insert_return_states(return_id, return_ranges, NOSPEC, param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

void check_nospec(int id)
{
	my_id = id;

	add_hook(&nospec_assign, ASSIGNMENT_HOOK);

	select_caller_info_hook(set_param_nospec, NOSPEC);


	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	add_split_return_callback(&returned_struct_members);
}
