/*
 * sparse/smatch_struct_assignment.c
 *
 * Copyright (C) 2014 Oracle.
 *
 * Licensed under the Open Software License version 1.1
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

static int known_struct_member_states(struct expression *expr)
{
	struct state_list *slist = __get_cur_slist();
	struct sm_state *sm;
	char *name;
	int ret = 0;
	int cmp;
	int len;

	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);

	name = expr_to_var(expr);
	if (!name)
		return 0;
	len = strlen(name);

	FOR_EACH_PTR(slist, sm) {
		if (sm->owner != SMATCH_EXTRA)
			continue;
		cmp = strncmp(sm->name, name, len);
		if (cmp < 0)
			continue;
		if (cmp == 0) {
			if (sm->name[len] == '.' ||
			    sm->name[len] == '-' ||
			    sm->name[len] == '.') {
				ret = 1;
				goto out;
			}
			continue;
		}
		goto out;
	} END_FOR_EACH_PTR(sm);

out:
	return ret;
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

void __struct_members_copy(int mode, struct expression *left, struct expression *right)
{
	struct symbol *struct_type, *tmp, *type;
	struct expression *left_member, *right_member, *assign;
	int op = '.';


	if (__in_fake_assign)
		return;

	left = strip_expr(left);
	right = strip_expr(right);

	struct_type = get_struct_type(left);
	if (!struct_type)
		return;

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
	__struct_members_copy(COPY_NORMAL, expr->left, expr->right);
}

static struct expression *remove_addr(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_PREOP && expr->op == '&')
		return strip_expr(expr->unop);
	return expr;
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

void register_struct_assignment(int id)
{
	add_function_hook("memset", &match_memset, NULL);

	add_function_hook("memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("memmove", &match_memcpy, INT_PTR(0));
}
