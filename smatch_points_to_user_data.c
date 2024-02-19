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
STATE(user_data_set);

struct user_fn_info {
	const char *name;
	int type;
	int param;
	const char *key;
};

// Old stuff that was here, but I no longer believe is user data
// kmap_atomic()
// skb_network_header

//	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
//	add_function_hook("usb_control_msg", &match_user_copy, INT_PTR(6));

static struct user_fn_info func_table[] = {
	{ "copy_from_user", USER_PTR_SET, 0, "$" },
	{ "__copy_from_user", USER_PTR_SET, 0, "$" },
	{ "kvm_read_guest_virt", USER_PTR_SET, 2, "$" },
	{ "vpu_iface_receive_msg", USER_PTR_SET, 1, "$" },
	{ "xdr_stream_decode_u32", USER_PTR_SET, 1, "$" },

	{ "(struct ksmbd_transport_ops)->read", USER_PTR_SET, 1, "$" },
	{ "nlmsg_data", USER_PTR_SET, -1, "$" },
	{ "nla_data", USER_PTR_SET, -1, "$" },
	{ "memdup_user", USER_PTR_SET, -1, "$" },
	{ "cfg80211_find_elem_match", USER_PTR_SET, -1, "$" },
	{ "ieee80211_bss_get_elem", USER_PTR_SET, -1, "$" },
	{ "cfg80211_find_elem", USER_PTR_SET, -1, "$" },
	{ "ieee80211_bss_get_ie", USER_PTR_SET, -1, "$" },

	{ "brcmf_fweh_dequeue_event", USER_PTR_SET, -1, "&$->emsg" },
	{ "wilc_wlan_rxq_remove", USER_PTR_SET, -1, "$->buffer" },
	{ "cfg80211_find_vendor_ie", USER_PTR_SET, -1, "$" },

	{ "xdr_copy_to_scratch", USER_PTR_SET, -1, "$" },
	{ "xdr_inline_decode", USER_PTR_SET, -1, "$" },
	{ "ntfs_read_run_nb", USER_PTR_SET, 3, "$" },
	{ "ntfs_read_bh", USER_PTR_SET, 3, "(0<~$0)" },
	{ "__wbuf", USER_PTR_SET, 1, "*$" },

	{ "kstrtoull", USER_PTR_SET, 2, "$" },
	{ "kstrtoll", USER_PTR_SET, 2, "$" },
	{ "kstrtoul", USER_PTR_SET, 2, "$" },
	{ "kstrtol", USER_PTR_SET, 2, "$" },
	{ "kstrtoint", USER_PTR_SET, 2, "$" },
	{ "kstrtou64", USER_PTR_SET, 2, "$" },
	{ "kstrtos64", USER_PTR_SET, 2, "$" },
	{ "kstrtou32", USER_PTR_SET, 2, "$" },
	{ "kstrtos32", USER_PTR_SET, 2, "$" },
	{ "kstrtou16", USER_PTR_SET, 2, "$" },
	{ "kstrtos16", USER_PTR_SET, 2, "$" },
	{ "kstrtou8", USER_PTR_SET, 2, "$" },
	{ "kstrtos8", USER_PTR_SET, 2, "$" },
	{ "kstrtoull_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtoll_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtoul_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtol_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtouint_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtoint_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtou16_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtos16_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtou8_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtos8_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtou64_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtos64_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtou32_from_user", USER_PTR_SET, 2, "$" },
	{ "kstrtos32_from_user", USER_PTR_SET, 2, "$" },
};

static struct user_fn_info call_table[] = {
	{ "__handle_ksmbd_work", USER_DATA, 0, "$->request_buf" },
};

bool is_skb_data(struct expression *expr)
{
	struct symbol *sym;

	expr = strip_expr(expr);
	if (!expr)
		return false;
	if (expr->type != EXPR_DEREF)
		return false;

	if (!expr->member)
		return false;
	if (strcmp(expr->member->name, "data") != 0)
		return false;

	sym = get_type(expr->deref);
	if (!sym)
		return false;
	if (sym->type == SYM_PTR)
		sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_STRUCT || !sym->ident)
		return false;
	if (strcmp(sym->ident->name, "sk_buff") != 0)
		return false;

	return true;
}

static bool is_array_of_user_data(struct expression *expr)
{
	struct expression *deref;
	struct symbol *type;

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		if (expr->type == EXPR_PREOP && expr->op == '*')
			expr = strip_expr(expr->unop);
	}

	/* This is for array elements &foo->data[4] */
	if (expr->type == EXPR_BINOP && expr->op == '+') {
		if (points_to_user_data(expr->left))
			return true;
		if (points_to_user_data(expr->right))
			return true;
	}

	/* This is for if you have: foo = skb->data; frob(foo->array); */
	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return false;

	if (expr->type != EXPR_DEREF)
		return false;
	deref = strip_expr(expr->deref);
	if (deref->type != EXPR_PREOP || deref->op != '*')
		return false;
	deref = strip_expr(deref->unop);
	return points_to_user_data(deref);
}

static struct expression *remove_addr_stuff(struct expression *expr)
{
	/* take "&foo->bar" and return "foo" */
	expr = strip_expr(expr);
	if (expr->type != EXPR_PREOP || expr->op != '&')
		return expr;
	expr = strip_expr(expr->unop);
	while (expr && expr->type == EXPR_DEREF) {
		expr = strip_expr(expr->deref);
		if (expr->op == '.')
			continue;
		else
			break;
	}
	if (expr->type == EXPR_PREOP && expr->op == '*')
		expr = strip_expr(expr->unop);
	return expr;
}

static bool math_points_to_user_data(struct expression *expr)
{
	struct sm_state *sm;

	// TODO: is_array_of_user_data() should probably be handled here

	if (expr->type == EXPR_BINOP && expr->op == '+')
		return math_points_to_user_data(expr->left);

	expr = remove_addr_stuff(expr);

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return false;
	if (slist_has_state(sm->possible, &user_data) ||
	    slist_has_state(sm->possible, &user_data_set))
		return true;
	return false;
}

bool points_to_user_data(struct expression *expr)
{
	struct sm_state *sm;

	expr = strip_expr(expr);
	if (!expr)
		return false;

	if (expr->type == EXPR_POSTOP)
		expr = strip_expr(expr->unop);

	if (is_fake_call(expr))
		return false;

	if (expr->type == EXPR_ASSIGNMENT)
		return points_to_user_data(expr->left);

	if (is_array_of_user_data(expr))
		return true;

	if (expr->type == EXPR_BINOP && expr->op == '+')
		return math_points_to_user_data(expr);

	if (is_skb_data(expr))
		return true;

	// FIXME if you have a struct pointer p then p->foo should be handled
	// by smatch_kernel_user_data.c but if you have (p + 1)->foo then this
	// should be handled here.
	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return false;
	if (slist_has_state(sm->possible, &user_data) ||
	    slist_has_state(sm->possible, &user_data_set))
		return true;
	return false;
}

void set_array_user_ptr(struct expression *expr, bool is_new)
{
	struct expression *tmp;

	/*
	 * If you have:
	 *         p = buf;
	 *         copy_from_user(p, src, 100);
	 * At the end, both "p" and "buf" point to user data.
	 *
	 */
	tmp = get_assigned_expr(expr);
	if (tmp)
		set_state_expr(my_id, tmp, is_new ? &user_data_set : &user_data);
	set_state_expr(my_id, expr, is_new ? &user_data_set : &user_data);
}

static void match_assign(struct expression *expr)
{
	if (is_fake_call(expr->right))
		return;

	if (!is_ptr_type(get_type(expr->left)))
		return;

	if (points_to_user_data(expr->right)) {
		// FIXME: if the types are different then mark the stuff on
		// the left as user data.
		set_state_expr(my_id, expr->left, &user_data);
		return;
	}

	// FIXME: just use a modification hook
	if (get_state_expr(my_id, expr->left))
		set_state_expr(my_id, expr->left, &undefined);
}

static void match_memcpy(const char *fn, struct expression *expr, void *_unused)
{
	struct expression *dest, *src;

	dest = get_argument_from_call_expr(expr->args, 0);
	src = get_argument_from_call_expr(expr->args, 1);

	if (points_to_user_data(src)) {
		set_state_expr(my_id, expr->left, &user_data_set);
		return;
	}

	if (get_state_expr(my_id, dest))
		set_state_expr(my_id, dest, &undefined);
}

static void fake_assign_helper(struct expression *expr, void *data)
{
	struct expression *left = expr->left;
	struct symbol *type;
	bool set = data;

	type = get_type(left);
	if (!type)
		return;
	if (type->type == SYM_BASETYPE)
		mark_as_user_data(left, set);
	else if (type->type == SYM_ARRAY)
		set_array_user_ptr(left, set);
}

static void returns_user_ptr_helper(struct expression *expr, const char *name, struct symbol *sym, bool set)
{
	struct expression *call, *arg;

	call = expr;
	while (call && call->type == EXPR_ASSIGNMENT)
		call = strip_expr(expr->right);
	if (!call || call->type != EXPR_CALL)
		return;

	if (!set && !we_pass_user_data(call))
		return;

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;

	create_recursive_fake_assignments(deref_expression(arg), &fake_assign_helper, INT_PTR(set));

	if (arg->type == EXPR_PREOP && arg->op == '&') {
		struct symbol *type;

		type = get_type(arg->unop);
		if (!type || type->type != SYM_ARRAY)
			return;
	}

	set_state_expr(my_id, arg, set ? &user_data_set : &user_data);
}

static void returns_user_ptr(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	returns_user_ptr_helper(expr, name, sym, false);
}

static void returns_user_ptr_set(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	returns_user_ptr_helper(expr, name, sym, true);
}

static void set_param_user_ptr(const char *name, struct symbol *sym, char *value)
{
	set_state(my_id, name, sym, &user_data);
}

static void set_caller_param_key_user_ptr(struct expression *expr, const char *name,
				    struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &user_data);
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (is_socket_stuff(sm->sym))
		return;

	if (!slist_has_state(sm->possible, &user_data) &&
	    !slist_has_state(sm->possible, &user_data_set))
		return;

	sql_insert_caller_info(call, USER_PTR, param, printed_name, "");
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	int type;

	/* is this even possible? */
	if (strcmp(printed_name, "&$") == 0)
		return;

	if (is_socket_stuff(sm->sym))
		return;

	if (param >= 0) {
		if (!slist_has_state(sm->possible, &user_data_set))
			return;
		type = USER_PTR_SET;
	} else {
		if (slist_has_state(sm->possible, &user_data_set))
			type = USER_PTR_SET;
		else if (slist_has_state(sm->possible, &user_data))
			type = USER_PTR;
		else
			return;
	}
	if (parent_is_gone_var_sym(sm->name, sm->sym))
		return;

	sql_insert_return_states(return_id, return_ranges, type,
				 param, printed_name, "");
}

void register_points_to_user_data(int id)
{
	struct user_fn_info *info;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_function_hook("memcpy", &match_memcpy, NULL);
	add_function_hook("__memcpy", &match_memcpy, NULL);

	add_caller_info_callback(my_id, caller_info_callback);
	add_return_info_callback(my_id, return_info_callback);

	select_caller_name_sym(set_param_user_ptr, USER_PTR);
	for (i = 0; i < ARRAY_SIZE(call_table); i++) {
		info = &call_table[i];
		add_function_param_key_hook_early(info->name,
						  &set_caller_param_key_user_ptr,
						  info->param, info->key, info);
	}

	select_return_param_key(USER_PTR, &returns_user_ptr);
	select_return_param_key(USER_PTR_SET, &returns_user_ptr_set);
	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];
		add_function_param_key_hook_late(info->name,
						 &returns_user_ptr_set,
						 info->param, info->key, info);
	}

}
