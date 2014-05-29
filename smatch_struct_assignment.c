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
 * I don't have a proper solution for this problem right now.  I just copy one
 * level and don't nest.  It should handle limitted nesting but intelligently.
 *
 * The other thing is that you end up with a lot of garbage assignments where
 * we record "x could be anything. x->y could be anything. x->y->z->a->b->c
 * could *also* be anything!".  There should be a better way to filter this
 * useless information.
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static struct symbol *get_struct_type(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return NULL;
	if (type->type == SYM_PTR)
		type = get_real_base_type(type);
	if (type && type->type == SYM_STRUCT)
		return type;
	return NULL;
}

static struct expression *get_matching_member_expr(struct symbol *left_type, struct expression *right, struct symbol *left_member)
{
	struct symbol *struct_type;
	int op = '.';

	if (!left_member->ident)
		return NULL;

	struct_type = get_struct_type(right);
	if (!struct_type)
		return NULL;
	if (struct_type != left_type)
		return NULL;

	if (right->type == EXPR_PREOP && right->op == '&')
		right = strip_expr(right->unop);

	if (is_pointer(right)) {
		right = deref_expression(right);
		op = '*';
	}

	return member_expression(right, op, left_member->ident);
}

static struct expression *remove_addr(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_PREOP && expr->op == '&')
		return strip_expr(expr->unop);
	return expr;
}

void __struct_members_copy(int mode, struct expression *left, struct expression *right)
{
	struct symbol *struct_type, *tmp, *type;
	struct expression *left_member;
	struct expression *right_member = NULL;
	struct expression *assign;
	int op = '.';


	if (__in_fake_assign)
		return;

	left = strip_expr(left);
	right = strip_expr(right);

	struct_type = get_struct_type(left);
	if (!struct_type) {
		/*
		 * This is not a struct assignment obviously.  But this is where
		 * memcpy() is handled so it feels like a good place to add this
		 * code.
		 */

		type = get_type(left);
		if (!type || type->type != SYM_BASETYPE)
			return;

		right = strip_expr(right);
		if (right && right->type == EXPR_PREOP && right->op == '&')
			right = remove_addr(right);
		else
			right = unknown_value_expression(left);
		assign = assign_expression(left, right);
		__in_fake_assign++;
		__split_expr(assign);
		__in_fake_assign--;
		return;
	}

	if (is_pointer(left)) {
		left = deref_expression(left);
		op = '*';
	}

	FOR_EACH_PTR(struct_type->symbol_list, tmp) {
		type = get_real_base_type(tmp);
		if (type && type->type == SYM_ARRAY)
			continue;

		left_member = member_expression(left, op, tmp->ident);

		switch (mode) {
		case COPY_NORMAL:
		case COPY_MEMCPY:
			right_member = get_matching_member_expr(struct_type, right, tmp);
			break;
		case COPY_MEMSET:
			right_member = right;
			break;
		}
		if (!right_member)
			right_member = unknown_value_expression(left_member);
		assign = assign_expression(left_member, right_member);
		__in_fake_assign++;
		__split_expr(assign);
		__in_fake_assign--;
	} END_FOR_EACH_PTR(tmp);
}

void __fake_struct_member_assignments(struct expression *expr)
{
	struct symbol *struct_type;

	if (is_zero(expr->right))
		return;

	struct_type = get_struct_type(expr->left);
	if (!struct_type)
		return;

	__struct_members_copy(COPY_NORMAL, expr->left, expr->right);
}

static void match_memset(const char *fn, struct expression *expr, void *_size_arg)
{
	struct expression *buf;
	struct expression *val;

	buf = get_argument_from_call_expr(expr->args, 0);
	val = get_argument_from_call_expr(expr->args, 1);

	buf = strip_expr(buf);
	__struct_members_copy(COPY_MEMSET, remove_addr(buf), val);
}

static void match_memcpy(const char *fn, struct expression *expr, void *_arg)
{
	struct expression *dest;
	struct expression *src;

	dest = get_argument_from_call_expr(expr->args, 0);
	src = get_argument_from_call_expr(expr->args, 1);

	__struct_members_copy(COPY_MEMCPY, remove_addr(dest), remove_addr(src));
}

static void match_memcpy_unknown(const char *fn, struct expression *expr, void *_arg)
{
	struct expression *dest;

	dest = get_argument_from_call_expr(expr->args, 0);
	__struct_members_copy(COPY_MEMCPY, remove_addr(dest), NULL);
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

static void db_param_cleared(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	if (strcmp(value, "0") == 0)
		__struct_members_copy(COPY_MEMSET, remove_addr(arg), zero_expr());
	else
		__struct_members_copy(COPY_MEMCPY, remove_addr(arg), NULL);

}

void register_struct_assignment(int id)
{
	add_function_hook("memset", &match_memset, NULL);

	add_function_hook("memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("memmove", &match_memcpy, INT_PTR(0));

	register_clears_param();
	select_return_states_hook(PARAM_CLEARED, &db_param_cleared);
}
