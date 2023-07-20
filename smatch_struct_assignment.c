/*
 * Copyright (C) 2014 Oracle.
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
 * This file started out by saying that if you have:
 *
 * 	struct foo one, two;
 * 	...
 * 	one = two;
 *
 * That's equivalent to saying:
 *
 * 	one.x = two.x;
 * 	one.y = two.y;
 *
 * Turning an assignment like that into a bunch of small fake assignments is
 * really useful.
 *
 * The call to memcpy(&one, &two, sizeof(foo)); is the same as "one = two;" so
 * we can re-use the code.  And we may as well use it for memset() too.
 * Assigning pointers is almost the same:
 *
 * 	p1 = p2;
 *
 * Is the same as:
 *
 * 	p1->x = p2->x;
 * 	p1->y = p2->y;
 *
 * The problem is that you can go a bit crazy with pointers to pointers.
 *
 * 	p1->x->y->z->one->two->three = p2->x->y->z->one->two->three;
 *
 * We probably want to distinguish between between shallow and deep copies so
 * that if we memset(p1, 0, sizeof(*p1)) then it just sets p1->x to zero and not
 * p1->x->y.
 *
 * I don't have a proper solution for this problem right now.  I just copy one
 * level and don't nest.  It should handle limitted nesting but intelligently.
 *
 * The other thing is that you end up with a lot of garbage assignments where
 * we record "x could be anything. x->y could be anything. x->y->z->a->b->c
 * could *also* be anything!".  There should be a better way to filter this
 * useless information.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

enum {
	COPY_NORMAL,
	COPY_UNKNOWN,
	COPY_ZERO,
};

static struct symbol *get_struct_type(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return NULL;
	if (type->type == SYM_PTR) {
		type = get_real_base_type(type);
		if (!type)
			return NULL;
	}
	if (type->type == SYM_STRUCT)
		return type;
	if (type->type == SYM_UNION)
		return type;
	return NULL;
}

static struct expression *get_right_base_expr(struct symbol *left_type, struct expression *right)
{
	struct symbol *struct_type;

	if (!right)
		return NULL;

	struct_type = get_struct_type(right);
	if (!struct_type)
		return NULL;
	if (struct_type != left_type)
		return NULL;

	if (right->type == EXPR_PREOP && right->op == '&')
		right = strip_expr(right->unop);

	if (right->type == EXPR_CALL)
		return NULL;

	return right;
}

static struct expression *add_dereference(struct expression *expr)
{
	struct symbol *type;

	/*
	 * We want to re-write "memcpy(foo, bar, sizeof(*foo));" as
	 * "*foo = *bar;".
	 */

	expr = strip_expr(expr);
	if (!expr)
		return NULL;

	if (expr->type == EXPR_PREOP && expr->op == '&')
		return strip_expr(expr->unop);
	type = get_type(expr);
	if (!type)
		return NULL;
	if (type->type != SYM_PTR && type->type != SYM_ARRAY)
		return NULL;

	return deref_expression(expr);
}

static struct expression *faked_expression;
struct expression *get_faked_expression(void)
{
	if (!__in_fake_assign)
		return NULL;
	return faked_expression;
}

static void split_fake_expr(struct expression *expr, void *unused)
{
	__in_fake_assign++;
	__in_fake_struct_assign++;
	__split_expr(expr);
	__in_fake_struct_assign--;
	__in_fake_assign--;
}

static void handle_non_struct_assignments(struct expression *left, struct expression *right,
			void (*assign_handler)(struct expression *expr, void *data),
			void *data)
{
	struct symbol *left_type, *right_type;
	struct expression *assign;

	while (right && right->type == EXPR_ASSIGNMENT)
		right = strip_parens(right->left);

	if (!right)
		right = unknown_value_expression(left);

	left_type = get_type(left);
	right_type = get_type(right);
	if (!left_type || !right_type)
		return;

	if (right_type->type == SYM_ARRAY)
		right = unknown_value_expression(left);

	if (left_type->type == SYM_PTR) {
		left = deref_expression(left);
		assign = assign_expression(left, '=', right);
		assign_handler(assign, data);
		return;
	}
	if (left_type->type != SYM_BASETYPE)
		return;
	assign = assign_expression(left, '=', right);
	assign_handler(assign, data);
}

static bool dont_care(int mode, struct stree *left_care, struct stree *right_care,
		      struct expression *left, struct expression *right)
{
	struct symbol *left_sym, *right_sym;
	int left_len = 0, right_len = 0;
	char *left_name = NULL;
	char *right_name = NULL;
	struct sm_state *sm;
	int ret = false;
	int len;

	if (mode == COPY_ZERO)
		return false;

	left_name = expr_to_str_sym(left, &left_sym);
	if (left_name && !strstr(left_name, "->"))
		goto free;
	if (left_name)
		left_len = strlen(left_name);

	ret = true;
	FOR_EACH_SM(left_care, sm) {
		len = strlen(sm->name);
		if (left_name && left_sym == sm->sym &&
		    len >= left_len &&
		    strncmp(sm->name, left_name, len)) {
			ret = false;
			goto free;
		}
	} END_FOR_EACH_SM(sm);

	right_name = expr_to_str_sym(right, &right_sym);
	if (right_name)
		right_len = strlen(right_name);
	if (!right_sym || left_sym == right_sym)
		goto free;

	FOR_EACH_SM(right_care, sm) {
		len = strlen(sm->name);
		if (right_name && right_sym == sm->sym &&
		    len >= right_len &&
		    strncmp(sm->name, right_name, len)) {
			ret = false;
			goto free;
		}
	} END_FOR_EACH_SM(sm);

free:
	free_string(left_name);
	free_string(right_name);
	return ret;
}

static void set_inner_struct_members(int mode, struct expression *faked,
		struct expression *left, struct expression *right, struct symbol *member,
		struct stree *care_left, struct stree *care_right,
		void (*assign_handler)(struct expression *expr, void *data),
		void *data)
{
	struct expression *left_member;
	struct expression *right_expr;
	struct expression *assign;
	struct symbol *base = get_real_base_type(member);
	struct symbol *tmp;

	if (member->ident) {
		left = member_expression(left, '.', member->ident);
		if (mode == COPY_NORMAL && right)
			right = member_expression(right, '.', member->ident);
	}

	if (dont_care(mode, care_left, care_right, left, right))
		return;

	FOR_EACH_PTR(base->symbol_list, tmp) {
		struct symbol *type;

		type = get_real_base_type(tmp);
		if (!type)
			continue;

		if (type->type == SYM_ARRAY)
			continue;
		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			set_inner_struct_members(mode, faked, left, right, tmp, care_left, care_right, assign_handler, data);
			continue;
		}
		if (!tmp->ident)
			continue;

		left_member = member_expression(left, '.', tmp->ident);

		right_expr = NULL;
		if (mode == COPY_NORMAL && right)
			right_expr = member_expression(right, '.', tmp->ident);
		if (mode == COPY_ZERO)
			right_expr = zero_expr();
		if (!right_expr)
			right_expr = unknown_value_expression(left_member);

		assign = assign_expression(left_member, '=', right_expr);
		assign_handler(assign, data);
	} END_FOR_EACH_PTR(tmp);
}

static void get_care_stree(struct expression *left, struct stree **left_care,
			   struct expression *right, struct stree **right_care)
{
	struct symbol *left_sym, *right_sym;
	struct sm_state *sm;

	*left_care = NULL;
	*right_care = NULL;

	left_sym = expr_to_sym(left);
	right_sym = expr_to_sym(right);

	FOR_EACH_SM(__get_cur_stree(), sm) {
		if (!sm->sym)
			continue;
		if (sm->sym == left_sym)
			overwrite_sm_state_stree(left_care, sm);
	} END_FOR_EACH_SM(sm);

	if (!right_sym || left_sym == right_sym)
		return;

	FOR_EACH_SM(__get_cur_stree(), sm) {
		if (!sm->sym)
			continue;
		if (sm->sym == right_sym)
			overwrite_sm_state_stree(right_care, sm);
	} END_FOR_EACH_SM(sm);
}

static void __struct_members_copy(int mode, struct expression *faked,
				  struct expression *left,
				  struct expression *right,
				  void (*assign_handler)(struct expression *expr, void *data),
				  void *data)
{
	struct symbol *struct_type, *tmp, *type;
	struct stree *care_left = NULL;
	struct stree *care_right = NULL;
	struct expression *left_member;
	struct expression *right_expr;
	struct expression *assign;

	if (__in_fake_assign || !left)
		return;

	faked_expression = faked;

	left = strip_expr(left);
	right = strip_expr(right);

	if (left->type == EXPR_PREOP && left->op == '*' && is_pointer(left))
		left = preop_expression(left, '(');

	struct_type = get_struct_type(left);
	if (!struct_type) {
		/*
		 * This is not a struct assignment obviously.  But this is where
		 * memcpy() is handled so it feels like a good place to add this
		 * code.
		 */
		handle_non_struct_assignments(left, right, assign_handler, data);
		goto done;
	}

	if (mode == COPY_NORMAL)
		right = get_right_base_expr(struct_type, right);

	if (mode != COPY_ZERO)
		get_care_stree(left, &care_left, right, &care_right);
	FOR_EACH_PTR(struct_type->symbol_list, tmp) {
		type = get_real_base_type(tmp);
		if (!type)
			continue;

		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			set_inner_struct_members(mode, faked, left, right, tmp,
						 care_left, care_right,
						 assign_handler, data);
			continue;
		}

		if (!tmp->ident)
			continue;

		left_member = member_expression(left, '.', tmp->ident);
		right_expr = NULL;

		if (mode == COPY_NORMAL && right)
			right_expr = member_expression(right, '.', tmp->ident);
		if (mode == COPY_ZERO)
			right_expr = zero_expr();
		if (!right_expr)
			right_expr = unknown_value_expression(left_member);

		assign = assign_expression(left_member, '=', right_expr);
		assign_handler(assign, data);
	} END_FOR_EACH_PTR(tmp);

	free_stree(&care_left);
	free_stree(&care_right);
done:
	faked_expression = NULL;
}

static int returns_zeroed_mem(struct expression *expr)
{
	struct expression *tmp;
	char *fn;

	if (expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL)
		return 0;

	if (is_fake_call(expr)) {
		tmp = get_faked_expression();
		if (!tmp || tmp->type != EXPR_ASSIGNMENT || tmp->op != '=')
			return 0;
		expr = tmp->right;
		if (expr->type != EXPR_CALL || expr->fn->type != EXPR_SYMBOL)
			return 0;
	}

	fn = expr_to_var(expr->fn);
	if (!fn)
		return 0;
	if (strcmp(fn, "kcalloc") == 0)
		return 1;
	if (option_project == PROJ_KERNEL && strstr(fn, "zalloc"))
		return 1;
	return 0;
}

static int copy_containter_states(struct expression *left, struct expression *right, int offset)
{
	char *left_name = NULL, *right_name = NULL;
	struct symbol *left_sym, *right_sym;
	struct sm_state *sm, *new_sm;
	int ret = 0;
	int len;
	char buf[64];
	char new_name[128];

	right_name = expr_to_var_sym(right, &right_sym);
	if (!right_name || !right_sym)
		goto free;
	left_name = expr_to_var_sym(left, &left_sym);
	if (!left_name || !left_sym)
		goto free;

	len = snprintf(buf, sizeof(buf), "%s(-%d)", right_name, offset);
	if (len >= sizeof(buf))
		goto free;

	FOR_EACH_SM_SAFE(__get_cur_stree(), sm) {
		if (sm->sym != right_sym)
			continue;
		if (strncmp(sm->name, buf, len) != 0)
			continue;
		snprintf(new_name, sizeof(new_name), "%s%s", left_name, sm->name + len);
		new_sm = clone_sm(sm);
		new_sm->name = alloc_sname(new_name);
		new_sm->sym = left_sym;
		__set_sm(new_sm);
		ret = 1;
	} END_FOR_EACH_SM_SAFE(sm);
free:
	free_string(left_name);
	free_string(right_name);
	return ret;
}

static int handle_param_offsets(struct expression *expr)
{
	struct expression *right;
	sval_t sval;

	right = strip_expr(expr->right);

	if (right->type != EXPR_BINOP || right->op != '-')
		return 0;

	if (!get_value(right->right, &sval))
		return 0;

	right = get_assigned_expr(right->left);
	if (!right)
		return 0;
	return copy_containter_states(expr->left, right, sval.value);
}

static void returns_container_of(struct expression *expr, int param, char *key, char *value)
{
	struct expression *call, *arg;
	int offset;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;
	call = strip_expr(expr->right);
	if (call->type != EXPR_CALL)
		return;
	if (param != -1)
		return;
	param = atoi(key);
	offset = atoi(value);

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;

	copy_containter_states(expr->left, arg, -offset);
}

void __fake_struct_member_assignments(struct expression *expr)
{
	struct expression *left, *right;
	struct symbol *type;
	int mode;

	if (expr->op != '=')
		return;

	if (is_noderef_ptr(expr->right))
		return;

	type = get_type(expr->left);
	if (!type)
		return;
	if (type->type != SYM_PTR && type->type != SYM_STRUCT)
		return;

	if (handle_param_offsets(expr))
		return;

	left = expr->left;
	right = expr->right;

	if (type->type == SYM_PTR) {
		/* Convert "p = q;" to "*p = *q;" */
		left = add_dereference(left);
		right = add_dereference(right);
	}

	if (returns_zeroed_mem(expr->right))
		mode = COPY_ZERO;
	else if (types_equiv(get_type(left), get_type(right)))
		mode = COPY_NORMAL;
	else
		mode = COPY_UNKNOWN;

	if (is_pointer(left)) {
		struct expression *assign;

		assign = assign_expression(left, '=', right);
		split_fake_expr(assign, 0);
	}

	__struct_members_copy(mode, expr, left, right, split_fake_expr, NULL);
}

static void match_memset(const char *fn, struct expression *expr, void *_size_arg)
{
	struct expression *buf;
	struct expression *val;
	int mode;

	buf = get_argument_from_call_expr(expr->args, 0);
	val = get_argument_from_call_expr(expr->args, 1);

	buf = strip_expr(buf);
	if (expr_is_zero(val))
		mode = COPY_ZERO;
	else
		mode = COPY_UNKNOWN;
	__struct_members_copy(mode, expr, add_dereference(buf), NULL, split_fake_expr, NULL);
}

static void match_memcpy(const char *fn, struct expression *expr, void *_arg)
{
	struct expression *dest;
	struct expression *src;
	int mode;

	dest = get_argument_from_call_expr(expr->args, 0);
	src = get_argument_from_call_expr(expr->args, 1);

	if (types_equiv(get_type(src), get_type(dest)))
		mode = COPY_NORMAL;
	else
		mode = COPY_UNKNOWN;

	__struct_members_copy(mode, expr, add_dereference(dest), add_dereference(src), split_fake_expr, NULL);
}

static void match_memdup(const char *fn, struct expression *call_expr,
			struct expression *expr, void *_unused)
{
	struct expression *left, *right, *arg;
	int mode;

	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return;

	left = strip_expr(expr->left);
	right = strip_expr(expr->right);

	if (right->type != EXPR_CALL)
		return;
	arg = get_argument_from_call_expr(right->args, 0);

	if (types_equiv(get_type(left), get_type(right)))
		mode = COPY_NORMAL;
	else
		mode = COPY_UNKNOWN;

	__struct_members_copy(mode, expr, add_dereference(left), add_dereference(arg), split_fake_expr, NULL);
}

static void match_memcpy_unknown(const char *fn, struct expression *expr, void *_arg)
{
	struct expression *dest;

	dest = get_argument_from_call_expr(expr->args, 0);
	__struct_members_copy(COPY_UNKNOWN, expr, add_dereference(dest), NULL, split_fake_expr, NULL);
}

static void match_sscanf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	int i;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		if (++i < 2)
			continue;
		__struct_members_copy(COPY_UNKNOWN, expr, add_dereference(arg), NULL, split_fake_expr, NULL);
	} END_FOR_EACH_PTR(arg);
}

static void unop_expr(struct expression *expr)
{
	if (expr->op != SPECIAL_INCREMENT &&
	    expr->op != SPECIAL_DECREMENT)
		return;

	if (!is_pointer(expr))
		return;
	faked_expression = expr;
	__struct_members_copy(COPY_UNKNOWN, expr, expr->unop, NULL, split_fake_expr, NULL);
	faked_expression = NULL;
}

static void register_clears_param(void)
{
	struct token *token;
	char name[256];
	const char *function;
	int param;

	if (option_project == PROJ_NONE)
		return;

	snprintf(name, 256, "%s.clears_argument", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		function = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		param = atoi(token->number);
		add_function_hook(function, &match_memcpy_unknown, INT_PTR(param));
		token = token->next;
	}
	clear_token_alloc();
}

void create_recursive_fake_assignments(struct expression *expr,
		void (*assign_handler)(struct expression *expr, void *data),
		void *data)
{
	__struct_members_copy(COPY_UNKNOWN, expr, expr, NULL, assign_handler, data);
}

static void db_buf_cleared(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;

	arg = gen_expr_from_param_key(expr, param, key);
	if (!arg)
		return;

	__in_buf_clear++;
	if (strcmp(value, "0") == 0)
		__struct_members_copy(COPY_ZERO, expr, arg, NULL, split_fake_expr, NULL);
	else
		__struct_members_copy(COPY_UNKNOWN, expr, arg, NULL, split_fake_expr, NULL);
	__in_buf_clear--;
}

void register_struct_assignment(int id)
{
	my_id = id;

	add_function_data((unsigned long *)&faked_expression);

	add_function_hook("memset", &match_memset, NULL);
	add_function_hook("__memset", &match_memset, NULL);
	add_function_hook("__builtin_memset", &match_memset, NULL);

	add_function_hook("memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("memmove", &match_memcpy, INT_PTR(0));
	add_function_hook("__memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("__memmove", &match_memcpy, INT_PTR(0));
	add_function_hook("__builtin_memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("__builtin_memmove", &match_memcpy, INT_PTR(0));

	if (option_project == PROJ_KERNEL) {
		return_implies_state_sval("kmemdup", valid_ptr_min_sval, valid_ptr_max_sval, &match_memdup, NULL);
		add_function_hook("copy_from_user", &match_memcpy, INT_PTR(0));
		add_function_hook("_copy_from_user", &match_memcpy, INT_PTR(0));
		add_function_hook("memcpy_fromio", &match_memcpy, INT_PTR(0));
		add_function_hook("__memcpy_fromio", &match_memcpy, INT_PTR(0));
	}

	add_function_hook("sscanf", &match_sscanf, NULL);

	add_hook(&unop_expr, OP_HOOK);
	register_clears_param();
	select_return_states_hook(BUF_CLEARED, &db_buf_cleared);
	select_return_states_hook(BUF_ADD, &db_buf_cleared);

	select_return_states_hook(CONTAINER, &returns_container_of);
}
