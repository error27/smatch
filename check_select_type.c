/*
 * Copyright (C) 2021 Oracle.
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
 * Type promotion with selects doesn't work how you might expect:
 * 	long foo = bar ? u_int_type : -12;
 * The -12 is promoted to unsigned int and the sign is not expanded.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static bool is_select(struct expression *expr)
{
	if (expr->type == EXPR_CONDITIONAL)
		return true;
	if (expr->type == EXPR_SELECT)
		return true;
	return false;
}

static int is_uint(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (type_positive_bits(type) == 32)
		return true;
	return false;
}

static int is_suspicious_int(struct expression *expr)
{
	struct symbol *type;
	struct range_list *rl;

	type = get_type(expr);
	if (type_positive_bits(type) != 31)
		return false;

	get_absolute_rl(expr, &rl);
	if (!sval_is_negative(rl_min(rl)))
		return false;

	if (expr->type == EXPR_BINOP && expr->op == SPECIAL_LEFTSHIFT)
		return false;

	return true;
}

static void match_assign(struct expression *expr)
{
	struct expression *right, *one, *two;
	struct symbol *type;
	char *name;

	if (expr->op != '=')
		return;

	right = strip_expr(expr->right);
	if (!is_select(right))
		return;

	type = get_type(expr->left);
	if (type_bits(type) != 64)
		return;

	if (right->cond_true)
		one = right->cond_true;
	else
		one = right->conditional;
	two = right->cond_false;

	if (is_uint(one) && is_suspicious_int(two)) {
		name = expr_to_str(two);
		sm_warning("check sign expansion for '%s'", name);
		free_string(name);
		return;
	}

	if (is_uint(two) && is_suspicious_int(one)) {
		name = expr_to_str(one);
		sm_warning("check sign expansion for '%s'", name);
		free_string(name);
		return;
	}
}

void check_select_type(int id)
{
	my_id = id;
	add_hook(match_assign, RAW_ASSIGNMENT_HOOK);
}
