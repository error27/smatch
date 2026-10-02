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

static int my_id;

STATE(unterminated);

static void set_unterminated(struct expression *expr)
{
	set_state_expr(my_id, expr, &unterminated);
}

static void match_memdup_user(const char *fn, struct expression *expr,
			      void *unused)
{
	set_unterminated(expr->left);
}

static bool copy_leaves_nul(struct expression *array,
			    struct expression *copy_size)
{
	struct expression *size_of_buf;
	int limit_type;
	int comparison;

	size_of_buf = get_size_variable(array, &limit_type);
	if (!size_of_buf)
		return false;
	comparison = get_comparison(size_of_buf, copy_size);

	if (limit_type == ELEM_LAST &&
	    (comparison == '<' || comparison == SPECIAL_UNSIGNED_LT ||
	     comparison == SPECIAL_LTE ||
	     comparison == SPECIAL_UNSIGNED_LTE ||
	     comparison == SPECIAL_EQUAL))
		return true;

	if (limit_type == BYTE_COUNT &&
	    (comparison == '<' || comparison == SPECIAL_UNSIGNED_LT))
		return true;

	return false;
}

static void match_copy_from_user(const char *fn, struct expression *expr,
				 void *unused)
{
	struct expression *dest, *bytes;
	struct range_list *rl;
	int size;

	dest = get_argument_from_call_expr(expr->args, 0);
	if (!dest)
		return;
	bytes = get_argument_from_call_expr(expr->args, 2);
	if (!bytes)
		goto set;

	size = get_array_size_bytes(dest);
	if (size > 0 && get_implied_rl(bytes, &rl) &&
	    rl_max(rl).uvalue < (unsigned long)size)
		return;
	if (copy_leaves_nul(dest, bytes))
		return;
set:
	set_unterminated(dest);
}

bool is_unterminated_user_string(struct expression *expr)
{
	struct sm_state *sm;

	if (__in_fake_parameter_assign)
		return false;

	sm = get_sm_state_expr(my_id, expr);
	if (sm)
		return slist_has_state(sm->possible, &unterminated);

	return is_skb_data(expr);
}

static void match_assign(struct expression *expr)
{
	if (expr->op != '=')
		return;
	if (is_unterminated_user_string(expr->right))
		set_unterminated(expr->left);
}

static void match_nul_terminate(struct expression *expr)
{
	if (!get_state_expr(my_id, expr))
		return;
	set_state_expr(my_id, expr, &undefined);
}

static void insert_caller_info(struct expression *call, int param,
				char *printed_name, struct sm_state *sm)
{
	if (!slist_has_state(sm->possible, &unterminated))
		return;

	sql_insert_caller_info(call, UNTERMINATED, param, printed_name, "");
}

static void select_caller_info(const char *name, struct symbol *sym,
				char *key, char *value)
{
	char fullname[256];

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, sizeof(fullname), "%s%s", name, key + 1);
	else
		return;

	set_state(my_id, fullname, sym, &unterminated);
}

static bool is_char_type(struct symbol *type)
{
	return type == &char_ctype || type == &schar_ctype || type == &uchar_ctype;
}

static bool is_char_buf(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type || (type->type != SYM_ARRAY && type->type != SYM_PTR))
		return false;
	type = get_real_base_type(type);
	return is_char_type(type);
}

static void set_struct_char_members(struct expression *expr, struct symbol *type)
{
	struct expression *member_expr;
	struct symbol *member;

	FOR_EACH_PTR(type->symbol_list, member) {
		if (!member->ident)
			continue;
		member_expr = member_expression(expr, '*', member->ident);
		if (!is_char_buf(member_expr))
			continue;
		set_unterminated(member_expr);
	} END_FOR_EACH_PTR(member);
}

static void set_zero_offset_unterminated(struct expression *expr)
{
	struct range_list *rl;

	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_BINOP || expr->op != '+')
		return;
	if (!is_pointer(expr->left))
		return;
	if (!get_implied_rl(expr->right, &rl))
		return;
	if (!rl_intersection(rl, alloc_rl(int_zero, int_zero)))
		return;
	set_unterminated(expr->left);
}

static void set_return_user_ptr(struct expression *expr, const char *name,
				struct symbol *sym, const char *value, void *data)
{
	struct expression *arg;
	struct symbol *type;

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;
	if (is_char_buf(arg)) {
		set_unterminated(arg);
		set_zero_offset_unterminated(arg);
		return;
	}

	type = get_type(arg);
	if (!type || type->type != SYM_PTR)
		return;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return;
	set_struct_char_members(arg, type);
}

static void set_return_user_data(struct expression *expr, const char *name,
				 struct symbol *sym, const char *value, void *data)
{
	struct expression *call;

	call = get_rightmost_call(expr);
	if (call && sym_name_is(call->fn, "copy_from_sockptr"))
		return;

	set_return_user_ptr(expr, name, sym, value, data);
}

void smatch_unterminated_user_string(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_assign_hook("memdup_user", &match_memdup_user, NULL);
	add_function_hook("copy_from_user", &match_copy_from_user, NULL);
	add_function_hook("__copy_from_user", &match_copy_from_user, NULL);
	add_function_hook("copy_from_sockptr", &match_copy_from_user, NULL);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_nul_terminate_callback(&match_nul_terminate);
	select_return_param_key(USER_PTR_SET, &set_return_user_ptr);
	select_return_param_key(USER_DATA_SET, &set_return_user_data);
	add_caller_info_callback(my_id, &insert_caller_info);
	select_caller_info_hook(&select_caller_info, UNTERMINATED);
}
