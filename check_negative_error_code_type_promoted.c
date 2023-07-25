/*
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

static void match_condition(struct expression *expr)
{
	struct expression *left, *right;
	char *name;
	int op;

	if (expr->type != EXPR_COMPARE)
		return;

	if (!flip_order(expr, &left, &op, &right))
		return;

	if (op != SPECIAL_UNSIGNED_LT &&
	    op != SPECIAL_UNSIGNED_LTE)
		return;

	if (!holds_kernel_error_codes(left))
		return;

	name = expr_to_str(left);
	sm_warning("error code type promoted to positive: '%s'", name);
	free_string(name);
}

void check_negative_error_code_type_promoted(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(&match_condition, CONDITION_HOOK);
}
