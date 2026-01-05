/*
 * Copyright (C) 2009 Dan Carpenter.
 * Copyright (C) 2018 Oracle.
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
#include "smatch_slist.h"

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
	struct statement *parent, *stmt, *else_cond;

	stmt = expr_get_parent_stmt(expr);
	if (!stmt || stmt->type != STMT_IF)
		return false;
	if (is_upper(var, stmt->if_conditional))
		return true;

	parent = stmt_get_parent_stmt(stmt);
	if (parent && parent->type == STMT_IF &&
	    parent->if_false == stmt &&
	    is_upper(var, parent->if_conditional))
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
	/* When --spammy is disabled then everything in a macro is silenced. */
	if (!option_spammy)
		return true;
	if (strcmp(macro, "ARRAY_SIZE") == 0 ||
	    strcmp(macro, "DPMCP_MIN_VER_MINOR") == 0 ||
	    strcmp(macro, "FIRST_USER_ADDRESS") == 0 ||
	    strcmp(macro, "KASAN_SHADOW_OFFSET") == 0 ||
	    strcmp(macro, "NF_CT_HELPER_BUILD_BUG_ON") == 0 ||
	    strcmp(macro, "STRTO_H") == 0 ||
	    strcmp(macro, "SUB_EXTEND_USTAT") == 0 ||
	    strcmp(macro, "TEST_CASTABLE_TO_TYPE_VAR") == 0 ||
	    strcmp(macro, "TEST_ONE_SHIFT") == 0 ||
	    strcmp(macro, "check_shl_overflow") == 0)
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

static bool is_special_x(struct expression *expr)
{
	if (option_project != PROJ_KERNEL)
		return false;
	if (sym_name_is("_x", expr))
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

	if (is_special_x(left))
		return;

	name = expr_to_str(left);
	sm_warning("unsigned '%s' is never less than zero.", name);
	free_string(name);
}

static int was_signed(struct expression *expr)
{
	struct expression *orig;
	struct range_list *rl;

	orig = get_assigned_expr(expr);
	if (!orig)
		return 0;
	if (!expr_signed(orig))
		return 0;

	get_absolute_rl(orig, &rl);
	return sval_is_negative(rl_min(rl));
}

static struct symbol *get_signed_type(struct symbol *type)
{
	switch (type_bits(type)) {
	case 8:
		return &char_ctype;
	case 16:
		return &ushort_ctype;
	case 32:
		return &int_ctype;
	case 64:
		return &llong_ctype;
	}
	return NULL;
}

static int has_individual_negatives(struct sm_state *sm)
{
	struct symbol *signed_type;
	struct sm_state *tmp;
	struct range_list *rl;

	signed_type = get_signed_type(estate_type(sm->state));
	if (!signed_type)
		return 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		rl = cast_rl(signed_type, estate_rl(tmp->state));
		if (sval_is_negative(rl_min(rl)) &&
		    sval_is_negative(rl_max(rl)))
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static void match_condition_lte(struct expression *expr)
{
	struct symbol *type;
	struct sm_state *sm;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;
	if (expr->op != SPECIAL_UNSIGNED_LTE &&
	    expr->op != SPECIAL_LTE)
		return;
	if (!expr_is_zero(expr->right))
		return;

	type = get_type(expr->left);
	if (!type || !type_unsigned(type))
		return;

	sm = get_extra_sm_state(expr->left);
	if (!sm)
		return;

	if (!was_signed(expr->left) &&
	    !has_individual_negatives(sm))
		return;

	if (has_upper_bound(expr->left, expr))
		return;

	name = expr_to_str(expr->left);
	sm_msg("warn: '%s' unsigned <= 0", name);
	free_string(name);
}

void check_unsigned_lt_zero(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_condition_lte, CONDITION_HOOK);
}
