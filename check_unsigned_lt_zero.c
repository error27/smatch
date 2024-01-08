/*
 * Copyright (C) 2009 Dan Carpenter.
 * Copyright 2023 Linaro Ltd.
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
#include "smatch_extra.h"

static int my_id;

static bool flip_order(struct expression *expr, struct expression **left, int *op, struct expression **right)
{
	/* flip everything to < */

	expr = strip_expr(expr);

	switch (expr->op) {
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		*left = strip_parens(expr->right);
		*op = flip_comparison(expr->op);
		*right = strip_parens(expr->left);
		return true;
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		*left = strip_parens(expr->left);
		*op = expr->op;
		*right = strip_parens(expr->right);
		return true;
	}

	return false;
}

static bool is_upper(struct expression *var, struct expression *cond)
{
	struct expression *left, *right;
	int op;

	cond = strip_expr(cond);

	if (!flip_order(cond, &left, &op, &right))
		return false;
	if (expr_equiv(var, right))
		return true;
	return false;
}

static bool is_if_else_clamp_stmt(struct expression *var, struct expression *expr)
{
	struct statement *stmt, *else_cond;

	stmt = expr_get_parent_stmt(expr);
	if (!stmt || stmt->type != STMT_IF)
		return false;
	if (is_upper(var, stmt->if_conditional))
		return true;

	else_cond = stmt->if_false;
	if (!else_cond || else_cond->type != STMT_IF)
		return false;
	if (is_upper(var, else_cond->if_conditional))
		return true;

	return false;
}

static bool is_allowed_zero(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return false;
	if (strcmp(macro, "ARRAY_SIZE") == 0 ||
	    strcmp(macro, "DPMCP_MIN_VER_MINOR") == 0 ||
	    strcmp(macro, "KASAN_SHADOW_OFFSET") == 0 ||
	    strcmp(macro, "NF_CT_HELPER_BUILD_BUG_ON") == 0 ||
	    strcmp(macro, "TEST_ONE_SHIFT") == 0)
		return true;
	return false;
}

static bool has_upper_bound(struct expression *var, struct expression *expr)
{
	struct expression *parent, *prev;

	parent = expr;
	prev = expr;
	while ((parent = expr_get_parent_expr(parent))) {
		if (parent->type == EXPR_LOGICAL &&
		    parent->op == SPECIAL_LOGICAL_AND)
			break;
		if (parent->type == EXPR_LOGICAL &&
		    parent->op == SPECIAL_LOGICAL_OR) {
			if (prev == parent->left &&
			    is_upper(var, parent->right))
				return true;
			if (prev == parent->right &&
			    is_upper(var, parent->left))
				return true;
		}
		prev = parent;
	}
	if (is_if_else_clamp_stmt(var, prev))
		return true;

	return false;
}

static void match_condition(struct expression *expr)
{
	struct expression *left, *right;
	char *name;
	int op;

	if (expr->type != EXPR_COMPARE)
		return;

	if (!flip_order(expr, &left, &op, &right))
		return;

	if (op != '<' &&
	    op != SPECIAL_UNSIGNED_LT)
		return;

	if (!expr_is_zero(right))
		return;

	if (is_allowed_zero(right))
		return;

	if (op != SPECIAL_UNSIGNED_LT && !expr_unsigned(left))
		return;

	if (has_upper_bound(left, expr))
		return;

	name = expr_to_str(left);
	sm_warning("unsigned '%s' is never less than zero.", name);
	free_string(name);
}

void check_unsigned_lt_zero(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
}
