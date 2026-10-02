/*
 * Copyright (C) 2026 Dan Carpenter.
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

static struct expr_fn_list *nul_terminate_hooks;

static void match_memchr(const char *fn, struct expression *expr, void *unused);
static void match_terminates_destination(const char *fn,
					 struct expression *expr, void *data);
static bool is_char_string(struct expression *expr);

struct terminates_string {
	const char *fn;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static struct terminates_string terminates_string[] = {
	{ "nla_strdup", -1, "$" },
	{ "nla_strscpy", 0, "$", NULL, NULL, &match_terminates_destination },
	{ "strscpy", 0, "$" },
	{ "strscpy_pad", 0, "$" },
	{ "sized_strscpy", 0, "$", NULL, NULL, &match_terminates_destination },
	{ "strndup_user", -1, "$" },
	{ "snprintf", 0, "$" },
	{ "scnprintf", 0, "$" },
	{ "btrfs_check_ioctl_vol_args_path", 0, "$->name",
	  &int_zero, &int_zero },
	{ "elem_id_matches", 0, "$->name", &int_one, &int_one },
	{ "memchr", 0, "$", NULL, NULL, &match_memchr },
	{ "kmemdup_nul", -1, "$" },
};

void add_nul_terminate_callback(expr_func *fn)
{
	add_ptr_list(&nul_terminate_hooks, fn);
}

static void nul_terminate(struct expression *string)
{
	struct expression *base;

	base = get_array_base(string);
	if (base)
		string = base;
	call_expr_fns(nul_terminate_hooks, string);
}

static void set_conditional_nul_terminate(struct expression *string,
					  bool true_path)
{
	struct stree *false_stree;
	struct sm_state *sm;

	__push_fake_cur_stree();
	nul_terminate(string);
	false_stree = __pop_fake_cur_stree();
	FOR_EACH_SM(false_stree, sm) {
		set_true_false_states(sm->owner, sm->name, sm->sym,
				      true_path ? sm->state : NULL,
				      true_path ? NULL : sm->state);
	} END_FOR_EACH_SM(sm);
	free_stree(&false_stree);
}

static void match_condition(struct expression *expr)
{
	struct expression *string;

	string = get_array_base(expr);
	if (!string || !is_char_string(string))
		return;

	set_conditional_nul_terminate(string, false);
}

static void match_strnlen_condition(struct expression *expr)
{
	struct expression *call, *string;
	bool true_path;

	if (expr->type != EXPR_COMPARE)
		return;

	switch (expr->op) {
	case SPECIAL_EQUAL:
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		true_path = false;
		break;
	case SPECIAL_NOTEQUAL:
	case '<':
	case SPECIAL_UNSIGNED_LT:
		true_path = true;
		break;
	default:
		return;
	}

	call = get_assigned_expr_recurse(expr->left);
	if (!call)
		call = strip_expr(expr->left);
	if (!call || call->type != EXPR_CALL ||
	    !sym_name_is(call->fn, "strnlen"))
		return;

	string = get_argument_from_call_expr(call->args, 0);
	if (!string)
		return;

	set_conditional_nul_terminate(string, true_path);
}

static void match_nla_data(const char *fn, struct expression *expr, void *unused)
{
	if (is_nl_data_nul_string(expr))
		nul_terminate(expr->left);
}

static bool is_char_string(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return false;
	type = get_real_base_type(type);
	return type == &char_ctype || type == &schar_ctype || type == &uchar_ctype;
}

static void match_nul_assign(struct expression *expr)
{
	struct expression *string;
	sval_t sval;

	if (expr->op != '=')
		return;
	if (!get_value(expr->right, &sval) || sval.value != 0)
		return;

	string = get_array_base(expr->left);
	if (!string || !is_char_string(string))
		return;
	nul_terminate(string);
}

static void match_memset(const char *fn, struct expression *expr, void *unused)
{
	struct expression *string, *val, *size_arg;
	sval_t zero, size;

	string = get_argument_from_call_expr(expr->args, 0);
	val = get_argument_from_call_expr(expr->args, 1);
	size_arg = get_argument_from_call_expr(expr->args, 2);
	if (!string || !val || !size_arg)
		return;
	if (!is_char_string(string))
		return;
	if (!get_value(val, &zero) || zero.value != 0)
		return;
	if (!get_value(size_arg, &size))
		return;
	if (get_array_size_bytes(string) != size.value)
		return;
	nul_terminate(string);
}

static void match_terminates_string(struct expression *expr)
{
	nul_terminate(expr);
}

static void match_terminates_destination(const char *fn,
					 struct expression *expr, void *data)
{
	struct terminates_string *info = data;
	struct expression *string;

	string = get_argument_from_call_expr(expr->args, info->param);
	if (!string)
		return;
	nul_terminate(string);
}

static void match_memchr(const char *fn, struct expression *expr, void *unused)
{
	struct expression *string, *character;
	sval_t sval;

	string = get_argument_from_call_expr(expr->args, 0);
	character = get_argument_from_call_expr(expr->args, 1);
	if (!string || !character)
		return;
	if (!get_value(character, &sval) || sval.value != 0)
		return;
	nul_terminate(string);
}

static void return_adds_terminator(struct expression *expr, int param,
				   char *key, char *value)
{
	struct expression *string;

	string = gen_expr_from_param_key(expr, param, key);
	if (!string)
		return;
	nul_terminate(string);
}

void smatch_terminate_string(int id)
{
	struct terminates_string *info;
	int i;

	add_hook(&match_nul_assign, ASSIGNMENT_HOOK);
	add_function_hook("memset", &match_memset, NULL);
	add_function_hook("__memset", &match_memset, NULL);
	if (option_project == PROJ_KERNEL)
		add_function_assign_hook("nla_data", &match_nla_data, NULL);

	for (i = 0; i < ARRAY_SIZE(terminates_string); i++) {
		info = &terminates_string[i];
		if (info->call_back) {
			add_function_hook_late(info->fn, info->call_back, info);
		} else if (info->implies_start) {
			return_implies_param_key_expr(info->fn,
					*info->implies_start, *info->implies_end,
					&match_terminates_string,
					info->param, info->key, info);
		} else {
			add_param_key_expr_hook(info->fn, &match_terminates_string,
						info->param, info->key, info);
		}
	}

	select_return_states_hook(ADDS_TERMINATOR, &return_adds_terminator);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_strnlen_condition, CONDITION_HOOK);
}
