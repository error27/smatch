/*
 * Copyright (C) 2009 Dan Carpenter.
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
 * Check for things which are signed but probably should be unsigned.
 *
 * Hm...  It seems like at this point in the processing, sparse makes all
 * bitfields unsigned.  Which is logical but not what GCC does.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	sval_t sval;
	sval_t max;
	sval_t min;
	char *left_name, *right_name;

	if (__in_fake_assign)
		return;
	if (is_fake_var_assign(expr))
		return;
	if (expr->op == SPECIAL_AND_ASSIGN || expr->op == SPECIAL_OR_ASSIGN)
		return;

	sym = get_type(expr->left);
	if (!sym || sym->type != SYM_BASETYPE) {
		//sm_msg("could not get type");
		return;
	}
	if (type_bits(sym) < 0 || type_bits(sym) >= 32) /* max_val limits this */
		return;
	if (!get_implied_value(expr->right, &sval))
		return;
	max = sval_type_max(sym);
	if (sym != &bool_ctype && sym != &uchar_ctype &&
	    sval_cmp(max, sval) < 0 &&
	    !(sval.value < 256 && max.value == 127)) {
		left_name = expr_to_str(expr->left);
		right_name = expr_to_str(expr->right);
		sm_warning("'%s' %s can't fit into %s '%s'",
		       right_name, sval_to_numstr(sval), sval_to_numstr(max), left_name);
		free_string(left_name);
	}
	min = sval_type_min(sym);
	if (sval_cmp_t(&llong_ctype, min, sval) > 0) {
		if (min.value == 0 && sval.value == -1) /* assigning -1 to unsigned variables is idiomatic */
			return;
		if (expr->right->type == EXPR_PREOP && expr->right->op == '~')
			return;
		if (expr->op == SPECIAL_SUB_ASSIGN || expr->op == SPECIAL_ADD_ASSIGN)
			return;
		if (sval_positive_bits(sval) == 7)
			return;
		left_name = expr_to_str(expr->left);
		if (min.value == 0) {
			sm_warning("assigning %s to unsigned variable '%s'",
			       sval_to_str(sval), left_name);
		} else {
			sm_warning("value %s can't fit into %s '%s'",
			       sval_to_str(sval), sval_to_str(min), left_name);
		}
		free_string(left_name);
	}
}

void check_signed(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
