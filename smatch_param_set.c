/*
 * Copyright (C) 2012 Oracle.
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

/*
 * This is for functions like:
 *
 * int foo(int *x)
 * {
 * 	if (*x == 42) {
 *		*x = 0;
 *		return 1;
 *	}
 * 	return 0;
 * }
 *
 * If we return 1 that means the value of *x has been set to 0.  If we return
 * 0 then we have left *x alone.
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return alloc_estate_empty();
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct smatch_state *state)
{
	if (get_param_num_from_sym(sym) < 0)
		return;
	set_state(my_id, name, sym, state);
}

/*
 * This relies on the fact that these states are stored so that
 * foo->bar is before foo->bar->baz.
 */
static int parent_set(struct string_list *list, const char *name)
{
	char *tmp;
	int len;
	int ret;

	FOR_EACH_PTR(list, tmp) {
		len = strlen(tmp);
		ret = strncmp(tmp, name, len);
		if (ret < 0)
			continue;
		if (ret > 0)
			return 0;
		if (name[len] == '-')
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct stree *stree;
	struct sm_state *sm;
	struct smatch_state *extra;
	int param;
	struct range_list *rl;
	const char *param_name;
	struct string_list *set_list = NULL;
	char *math_str;
	char buf[256];

	stree = __get_cur_stree();

	FOR_EACH_MY_SM(my_id, stree, sm) {
		if (!estate_rl(sm->state))
			continue;
		extra = get_state(SMATCH_EXTRA, sm->name, sm->sym);
		if (!estate_rl(extra))
			continue;
		rl = rl_intersection(estate_rl(sm->state), estate_rl(extra));
		if (!rl)
			continue;

		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;
		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		if (strcmp(param_name, "$$") == 0)
			continue;

		math_str = get_value_in_terms_of_parameter_math_var_sym(sm->name, sm->sym);
		if (math_str) {
			snprintf(buf, sizeof(buf), "%s[%s]", show_rl(rl), math_str);
			insert_string(&set_list, (char *)sm->name);
			sql_insert_return_states(return_id, return_ranges, ADDED_VALUE,
					param, param_name, buf);
			continue;
		}

		/* no useful information here. */
		if (is_whole_rl(rl) && parent_set(set_list, sm->name))
			continue;
		insert_string(&set_list, (char *)sm->name);

		sql_insert_return_states(return_id, return_ranges, ADDED_VALUE,
					 param, param_name, show_rl(rl));
	} END_FOR_EACH_SM(sm);

	free_ptr_list((struct ptr_list **)&set_list);
}

int param_was_set(struct expression *expr)
{
	if (get_state_expr(my_id, expr))
		return 1;
	return 0;
}

void register_param_set(int id)
{
	my_id = id;

	add_extra_mod_hook(&extra_mod_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);
	add_split_return_callback(&print_return_value_param);
}

