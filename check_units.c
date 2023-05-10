/*
 * Copyright (C) 2018 Oracle.
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
#include "smatch_slist.h"

static int my_id;

static void check_mult(struct expression *expr)
{
	struct smatch_state *left, *right;
	int bit_found = 0, byte_found = 0;
	char *name;

	left = get_units(expr->left);
	right = get_units(expr->right);

	if (left == &unit_bit || right == &unit_bit)
		bit_found++;
	if (left == &unit_byte || right == &unit_byte)
		byte_found++;

	if (bit_found && byte_found) {
		name = expr_to_str(expr);
		sm_warning("multiplying bits * bytes '%s'", name);
		free_string(name);
	}
}

static void check_add_sub(struct expression *expr)
{
	struct smatch_state *left, *right;
	struct symbol *type;
	char *str;

	type = get_type(expr->left);
	if (type && (type->type == SYM_PTR || type->type == SYM_ARRAY))
		return;

	left = get_units(expr->left);
	right = get_units(expr->right);

	if (!left || !right || left == right)
		return;
	str = expr_to_str(expr);
	sm_warning("missing conversion: '%s' '%s %s %s'", str, left->name, show_special(expr->op), right->name);
	free_string(str);
}

static void match_binop_check(struct expression *expr)
{
	switch (expr->op) {
	case '+':
	case '-':
		check_add_sub(expr);
		return;
	case '*':
		check_mult(expr);
		return;
	}
}

static void match_condition_check(struct expression *expr)
{
	struct smatch_state *left, *right;
	char *str;

	if (expr->type != EXPR_COMPARE)
		return;

	left = get_units(expr->left);
	right = get_units(expr->right);

	if (!left || !right)
		return;
	if (left == right)
		return;

	str = expr_to_str(expr);
	sm_msg("warn: comparing different units: '%s' '%s %s %s'", str, left->name, show_special(expr->op), right->name);
	free_string(str);
}

void check_units(int id)
{
	my_id = id;

	if (!option_spammy)
		return;

	add_hook(&match_binop_check, BINOP_HOOK);
	add_hook(&match_condition_check, CONDITION_HOOK);
}
