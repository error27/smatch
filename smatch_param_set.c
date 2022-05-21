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

static int parent_is_set(const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct expression *faked;
	char *left_name;
	int ret = 0;
	int len;

	if (!__in_fake_assign)
		return 0;
	if (!is_whole_rl(estate_rl(state)))
		return 0;
	if (get_state(my_id, name, sym))
		return 0;

	faked = get_faked_expression();
	if (!faked)
		return 0;
	if ((faked->type == EXPR_PREOP || faked->type == EXPR_POSTOP) &&
	    (faked->op == SPECIAL_INCREMENT || faked->op == SPECIAL_DECREMENT)) {
		faked = strip_expr(faked->unop);
		if (faked->type == EXPR_SYMBOL)
			return 1;
		return 0;
	}
	if (faked->type != EXPR_ASSIGNMENT)
		return 0;

	left_name = expr_to_var(faked->left);
	if (!left_name)
		return 0;

	len = strlen(left_name);
	if (strncmp(name, left_name, len) == 0 && name[len] == '-')
		ret = 1;
	free_string(left_name);

	return ret;
}

static bool is_probably_worthless(struct expression *expr)
{
	struct expression *faked;

	if (!__in_fake_struct_assign)
		return false;

	faked = get_faked_expression();
	if (!faked || faked->type != EXPR_ASSIGNMENT)
		return false;

	if (faked->left->type == EXPR_PREOP &&
	    faked->left->op == '*')
		return false;

	return true;
}

static bool name_is_sym_name(const char *name, struct symbol *sym)
{
	if (!name || !sym || !sym->ident)
		return false;

	return strcmp(name, sym->ident->name) == 0;
}

static void extra_mod_hook(const char *name, struct symbol *sym, struct expression *expr, struct smatch_state *state)
{
	struct symbol *param_sym;
	struct symbol *type;
	char *param_name;

	if (expr && expr->smatch_flags & Fake)
		return;

	if (is_probably_worthless(expr))
		return;

	type = get_type(expr);
	if (type && (type->type == SYM_STRUCT || type->type == SYM_UNION))
		return;

	if (name_is_sym_name(name, sym))
		return;

	param_name = get_param_var_sym_var_sym(name, sym, NULL, &param_sym);
	if (!param_name || !param_sym)
		goto free;
	if (get_param_num_from_sym(param_sym) < 0)
		goto free;
	if (parent_is_set(param_name, param_sym, state))
		return;

	set_state(my_id, param_name, param_sym, state);
free:
	free_string(param_name);
}

/*
 * This function is is a dirty hack because extra_mod_hook is giving us a NULL
 *  sym instead of a vsl.
 */
static void match_array_assignment(struct expression *expr)
{
	struct expression *array, *offset;
	char *name;
	struct symbol *sym;
	struct range_list *rl;
	sval_t sval;
	char buf[256];

	if (__in_fake_assign)
		return;

	if (!is_array(expr->left))
		return;
	array = get_array_base(expr->left);
	offset = get_array_offset(expr->left);

	/* These are handled by extra_mod_hook() */
	if (get_value(offset, &sval))
		return;
	name = expr_to_var_sym(array, &sym);
	if (!name || !sym)
		goto free;
	if (map_to_param(name, sym) < 0)
		goto free;
	get_absolute_rl(expr->right, &rl);
	rl = cast_rl(get_type(expr->left), rl);

	snprintf(buf, sizeof(buf), "*%s", name);
	set_state(my_id, buf, sym, alloc_estate_rl(rl));
free:
	free_string(name);
}

static char *get_two_dots(const char *name)
{
	static char buf[80];
	int i, cnt = 0;

	for (i = 0; i < sizeof(buf); i++) {
		if (name[i] == '.') {
			cnt++;
			if (cnt >= 2) {
				buf[i] = '\0';
				return buf;
			}
		}
		buf[i] = name[i];
	}
	return NULL;
}

/*
 * This relies on the fact that these states are stored so that
 * foo->bar is before foo->bar->baz.
 */
static int parent_set(struct string_list *list, const char *param_name, struct sm_state *sm)
{
	char *tmp;
	int len;
	int ret;

	if (strncmp(param_name, "(*$)->", 6) == 0 && sm->sym && sm->sym->ident) {
		char buf[64];

		snprintf(buf, sizeof(buf), "*%s", sm->sym->ident->name);
		if (get_state(my_id, buf, sm->sym))
			return true;
	}

	FOR_EACH_PTR(list, tmp) {
		len = strlen(tmp);
		ret = strncmp(tmp, sm->name, len);
		if (ret < 0)
			continue;
		if (ret > 0)
			return 0;
		if (sm->name[len] == '-')
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static void print_return_value_param_helper(int return_id, char *return_ranges, struct expression *expr, int limit)
{
	struct sm_state *sm;
	struct smatch_state *extra;
	int param;
	struct range_list *rl;
	const char *param_name;
	struct string_list *set_list = NULL;
	char *math_str;
	char buf[256];
	char two_dot[80] = "";
	int count = 0;

	__promote_sets_to_clears(return_id, return_ranges, expr);

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		bool untracked = false;

		if (!estate_rl(sm->state))
			continue;
		extra = __get_state(SMATCH_EXTRA, sm->name, sm->sym);
		if (extra) {
			rl = rl_intersection(estate_rl(sm->state), estate_rl(extra));
			if (!rl)
				untracked = true;
		} else {
			rl = estate_rl(sm->state);
		}

		param = get_param_key_from_sm(sm, NULL, &param_name);
		if (param < 0 || !param_name)
			continue;
		if (param_name[0] == '&')
			continue;
		if (strcmp(param_name, "$") == 0 ||
		    is_recursive_member(param_name) ||
		    is_ignored_kernel_data(param_name)) {
			insert_string(&set_list, (char *)sm->name);
			continue;
		}
		if (untracked) {
			if (parent_was_PARAM_CLEAR(sm->name, sm->sym))
				continue;

			sql_insert_return_states(return_id, return_ranges,
						 UNTRACKED_PARAM, param, param_name, "");
			continue;
		}

		if (limit) {
			char *new = get_two_dots(param_name);

			/* no useful information here. */
			if (is_whole_rl(rl) && parent_set(set_list, param_name, sm))
				continue;

			if (new) {
				if (strcmp(new, two_dot) == 0)
					continue;

				strncpy(two_dot, new, sizeof(two_dot));
				insert_string(&set_list, (char *)sm->name);
				sql_insert_return_states(return_id, return_ranges,
					 PARAM_SET, param, new, "s64min-s64max");
				continue;
			}
		}

		math_str = get_value_in_terms_of_parameter_math_var_sym(sm->name, sm->sym);
		if (math_str) {
			snprintf(buf, sizeof(buf), "%s[%s]", show_rl(rl), math_str);
			insert_string(&set_list, (char *)sm->name);
			sql_insert_return_states(return_id, return_ranges,
					param_has_filter_data(sm) ? PARAM_ADD : PARAM_SET,
					param, param_name, buf);
			continue;
		}

		/* no useful information here. */
		if (is_whole_rl(rl) && parent_set(set_list, param_name, sm))
			continue;
		if (is_whole_rl(rl) && parent_was_PARAM_CLEAR(sm->name, sm->sym))
			continue;
		if (rl_is_zero(rl) && parent_was_PARAM_CLEAR_ZERO(sm->name, sm->sym))
			continue;

		insert_string(&set_list, (char *)sm->name);

		sql_insert_return_states(return_id, return_ranges,
					 param_has_filter_data(sm) ? PARAM_ADD : PARAM_SET,
					 param, param_name, show_rl(rl));
		if (limit && ++count > limit)
			break;

	} END_FOR_EACH_SM(sm);

	free_ptr_list((struct ptr_list **)&set_list);
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	print_return_value_param_helper(return_id, return_ranges, expr, 0);
}

void print_limited_param_set(int return_id, char *return_ranges, struct expression *expr)
{
	print_return_value_param_helper(return_id, return_ranges, expr, 1000);
}

static int possibly_empty(struct sm_state *sm)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (strcmp(tmp->name, "") == 0)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static bool sym_was_set(struct symbol *sym)
{
	char buf[80];

	if (!sym || !sym->ident)
		return false;

	snprintf(buf, sizeof(buf), "%s orig", sym->ident->name);
	if (get_comparison_strings(sym->ident->name, buf) == SPECIAL_EQUAL)
		return false;

	return true;
}

int param_was_set_var_sym(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	char buf[80];
	int len, i;

	if (!name)
		return 0;

	if (name[0] == '&')
		name++;

	if (sym_was_set(sym))
		return true;

	len = strlen(name);
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;

	for (i = 0; i <= len; i++) {
		if (name[i] != '-' && name[i] != '\0')
			continue;

		memcpy(buf, name, i);
		buf[i] = '\0';

		sm = get_sm_state(my_id, buf, sym);
		if (!sm)
			continue;
		if (possibly_empty(sm))
			continue;
		return 1;
	}

	if (name[0] == '*')
		return param_was_set_var_sym(name + 1, sym);

	return 0;
}

static struct expression *get_unfaked_expr(struct expression *expr)
{
	struct expression *tmp;

	if (!is_fake_var(expr))
		return expr;
	tmp = expr_get_fake_parent_expr(expr);
	if (!tmp || tmp->type != EXPR_ASSIGNMENT)
		return expr;
	return tmp->right;
}

int param_was_set(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	int ret = 0;

	expr = get_unfaked_expr(expr);

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	ret = param_was_set_var_sym(name, sym);
free:
	free_string(name);
	return ret;
}

void register_param_set(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_extra_mod_hook(&extra_mod_hook);
	add_hook(match_array_assignment, ASSIGNMENT_HOOK);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_estates);
	add_split_return_callback(&print_return_value_param);
}

