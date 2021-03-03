/*
 * Copyright (C) 2020 Oracle.
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
 * The problem here is that we can have:
 *
 * p = skb->data;
 *
 * In the olden days we would just set "*p = 0-255" which meant that it pointed
 * to user data.  But then if we say "if (*p == 11) {" that means that "*p" is
 * not user data any more, so then "*(p + 1)" is marked as not user data but it
 * is.
 *
 * So now we've separated out the stuff that points to a user_buf from the other
 * user data.
 *
 * There is a further complication because what if "p" points to a struct?  In
 * that case all the struct members are handled by smatch_kernel_user_data.c
 * but we still need to keep in mind that "*(p + 1)" is user data.  I'm not
 * totally 100% sure how this will work.
 *
 * Generally a user pointer should be a void pointer, or an array etc.  But if
 * it points to a struct that can only be used for pointer math.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
STATE(user_data);

static const char *returns_pointer_to_user_data[] = {
	"nlmsg_data", "nla_data", "memdup_user", "kmap_atomic", "skb_network_header",
	"cfg80211_find_elem_match", "ieee80211_bss_get_elem", "cfg80211_find_elem",
	"ieee80211_bss_get_ie",
};

bool is_skb_data(struct expression *expr)
{
	struct symbol *sym;

	expr = strip_expr(expr);
	if (!expr)
		return false;

	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (expr->type != EXPR_DEREF || expr->op != '.')
		return false;

	if (!expr->member)
		return false;
	if (strcmp(expr->member->name, "data") != 0)
		return false;

	sym = expr_to_sym(expr->deref);
	if (!sym)
		return false;
	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_PTR)
		return false;
	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_STRUCT || !sym->ident)
		return false;
	if (strcmp(sym->ident->name, "sk_buff") != 0)
		return false;

	return true;
}

bool is_user_data_fn(struct symbol *fn)
{
	int i;

	if (!fn || !fn->ident)
		return false;

	for (i = 0; i < ARRAY_SIZE(returns_pointer_to_user_data); i++) {
		if (strcmp(fn->ident->name, returns_pointer_to_user_data[i]) == 0) {
//			func_gets_user_data = true;
			return true;
		}
	}
	return false;
}

static bool is_points_to_user_data_fn(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol)
		return false;
	return is_user_data_fn(expr->fn->symbol);
}

static bool is_array_of_user_data(struct expression *expr)
{
	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		if (expr->type == EXPR_PREOP && expr->op == '*')
			expr = strip_expr(expr->unop);
	}

	if (expr->type == EXPR_BINOP && expr->op == '+') {
		if (points_to_user_data(expr->left))
			return true;
		if (points_to_user_data(expr->right))
			return true;
	}

	return false;
}

bool points_to_user_data(struct expression *expr)
{
	struct sm_state *sm;

	expr = strip_expr(expr);
	if (!expr)
		return false;

	if (expr->type == EXPR_ASSIGNMENT)
		return points_to_user_data(expr->left);

	if (is_array_of_user_data(expr))
		return true;

	if (expr->type == EXPR_BINOP && expr->op == '+')
		expr = strip_expr(expr->left);

	if (is_skb_data(expr))
		return true;

	if (is_points_to_user_data_fn(expr))
		return true;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &user_data))
		return true;
	return false;
}

void set_points_to_user_data(struct expression *expr)
{
	set_state_expr(my_id, expr, &user_data);
}

static bool handle_memcpy_fake_assignments(struct expression *expr)
{
	struct expression *left = strip_parens(expr->left);
	struct expression *right = strip_parens(expr->right);

	/* memcpy(array, src, sizeof(array)) gets turned into *array = *src; */

	if (left->type != EXPR_PREOP || left->op != '*')
		return false;
	if (right->type != EXPR_PREOP || right->op != '*')
		return false;
	left = strip_expr(left->unop);
	right = strip_expr(right->unop);

	if (points_to_user_data(right)) {
		set_points_to_user_data(left);
		return true;
	}
	return false;
}

static void match_assign(struct expression *expr)
{
	if (!is_ptr_type(get_type(expr->left)))
		return;

	if (points_to_user_data(expr->right)) {
		set_points_to_user_data(expr->left);
		return;
	}

	if (handle_memcpy_fake_assignments(expr))
		return;

	if (get_state_expr(my_id, expr->left))
		set_state_expr(my_id, expr->left, &undefined);
}

static void match_user_copy(const char *fn, struct expression *expr, void *_unused)
{
	struct expression *dest, *size;
	sval_t sval;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest = strip_expr(dest);
	if (!dest)
		return;

	size = get_argument_from_call_expr(expr->args, 2);
	if (get_implied_value(size, &sval))
		return;

	set_state_expr(my_id, dest, &user_data);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	int type = USER_PTR_SET;

	if (!slist_has_state(sm->possible, &user_data))
		return;

	if (param >= 0) {
		if (get_state_stree(get_start_states(), my_id, sm->name, sm->sym))
			return;
	} else {
		if (!param_was_set_var_sym(sm->name, sm->sym))
			type = USER_PTR;
	}
	if (parent_is_gone_var_sym(sm->name, sm->sym))
		return;

	sql_insert_return_states(return_id, return_ranges, type,
				 param, printed_name, "");
}

static void returns_user_ptr_helper(struct expression *expr, int param, char *key, char *value, bool set)
{
	struct expression *arg;
	struct expression *call;
	char *name;
	struct symbol *sym;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!set && !we_pass_user_data(call))
		return;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT) {
			/* Nothing to do.  Fake assignments should handle it */
			return;
		}
		arg = expr->left;
		goto set_user;
	}

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;
set_user:
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;
	set_state(my_id, name, sym, &user_data);
free:
	free_string(name);
}

static void returns_user_ptr(struct expression *expr, int param, char *key, char *value)
{
	returns_user_ptr_helper(expr, param, key, value, false);
}

static void returns_user_ptr_set(struct expression *expr, int param, char *key, char *value)
{
	returns_user_ptr_helper(expr, param, key, value, true);
}

static void set_param_user_ptr(const char *name, struct symbol *sym, char *key, char *value)
{
	struct expression *expr;
	char *fullname;

	expr = symbol_expression(sym);
	fullname = get_variable_from_key(expr, key, NULL);
	if (!fullname)
		return;
	set_state(my_id, fullname, sym, &user_data);
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (!slist_has_state(sm->possible, &user_data))
		return;
	sql_insert_caller_info(call, USER_PTR, param, printed_name, "");
}

void register_points_to_user_data(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_function_hook("copy_from_user", &match_user_copy, NULL);
	add_function_hook("memcpy_from_msg", &match_user_copy, NULL);
	add_function_hook("__copy_from_user", &match_user_copy, NULL);

	add_caller_info_callback(my_id, caller_info_callback);
	add_return_info_callback(my_id, return_info_callback);

	select_caller_info_hook(set_param_user_ptr, USER_PTR);
	select_return_states_hook(USER_PTR, &returns_user_ptr);
	select_return_states_hook(USER_PTR_SET, &returns_user_ptr_set);
}
