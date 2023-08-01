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

static const char *kernel_macros[] = {
	"DP_FFE_PRESET_MAX_LEVEL",
	"FSMC_THOLD_MASK",
	"FSMC_TSET_MASK",
	"FSMC_TWAIT_MASK",
	"L2CAP_CID_DYN_END",
	"LOGICVC_DIMENSIONS_MAX",
	"MAXREFCEXTLEN",
	"MAXREFCOUNT",
	"NLM4_OFFSET_MAX",
	"TIPC_MAX_PORT",
	"TSL2591_ALS_MAX_VALUE",
	"UBIFS_COMPR_TYPES_CNT",
	"XFS_MAX_CRC_AG_BLOCKS",
	NULL
};

static const char **allowed_macros;

static bool is_lt_zero(struct expression *expr)
{
	if (expr->type != EXPR_COMPARE)
		return false;
	if (expr->op != '<' && expr->op != SPECIAL_UNSIGNED_LT)
		return false;
	if (!expr_is_zero(expr->right))
		return false;
	return true;
}

static bool check_is_ulong_max_recursive(struct expression *expr)
{
	sval_t sval;

	expr = strip_expr(expr);

	if (!get_value(expr, &sval))
		return false;

	if (expr->type == EXPR_BINOP) {
		if (check_is_ulong_max_recursive(expr->left))
			return true;
		return false;
	}

	if (sval_cmp(sval, sval_type_max(&ulong_ctype)) == 0)
		return true;
	return false;
}

static bool is_u64_vs_ulongmax(struct expression *expr)
{
	struct symbol *left, *right;

	if (expr->op != '>' && expr->op != SPECIAL_UNSIGNED_GT)
		return false;
	if (!check_is_ulong_max_recursive(expr->right))
		return false;

	left = get_type(expr->left);
	right = get_type(expr->right);

	if (left == right)
		return true;
	if (type_positive_bits(left) < type_positive_bits(right))
		return true;

	if (type_bits(left) != 64)
		return false;
	if (right != &ulong_ctype && right != &uint_ctype)
		return false;

	return true;
}

static bool is_allowed_impossible_limit(struct expression *expr)
{
	char *macro;
	int i;

	if (!allowed_macros)
		return false;

	macro = get_macro_name(expr->pos);
	if (!macro) {
		macro = pos_ident(expr->pos);
		if (!macro)
			return false;
	}
	i = -1;
	while (allowed_macros[++i]) {
		if (strcmp(macro, allowed_macros[i]) == 0)
			return true;
	}
	return false;
}

static void match_condition(struct expression *expr)
{
	struct symbol *type;
	sval_t known;
	sval_t min, max;
	struct range_list *rl_left_orig, *rl_right_orig;
	struct range_list *rl_left, *rl_right;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;

	/* handled by check_unsigned_lt_zero.c */
	if (is_lt_zero(expr))
		return;

	type = get_type(expr);
	if (!type)
		return;

	if (get_macro_name(expr->pos))
		return;

	/* check that one and only one side is known */
	if (get_value(expr->left, &known)) {
		if (get_value(expr->right, &known))
			return;
		rl_left_orig = alloc_rl(known, known);
		rl_left = cast_rl(type, rl_left_orig);

		min = sval_type_min(get_type(expr->right));
		max = sval_type_max(get_type(expr->right));
		rl_right_orig = alloc_rl(min, max);
		rl_right = cast_rl(type, rl_right_orig);
	} else if (get_value(expr->right, &known)) {
		rl_right_orig = alloc_rl(known, known);
		rl_right = cast_rl(type, rl_right_orig);

		min = sval_type_min(get_type(expr->left));
		max = sval_type_max(get_type(expr->left));
		rl_left_orig = alloc_rl(min, max);
		rl_left = cast_rl(type, rl_left_orig);
	} else {
		return;
	}

	if (possibly_true_rl(rl_left, expr->op, rl_right))
		return;
	if (is_u64_vs_ulongmax(expr))
		return;
	if (is_allowed_impossible_limit(expr->right))
		return;

	name = expr_to_str(expr);
	sm_warning("impossible condition '(%s) => (%s %s %s)'", name,
		   show_rl(rl_left), show_special(expr->op), show_rl(rl_right));
	free_string(name);
}

void check_impossible_compare(int id)
{
	my_id = id;

	if (option_project == PROJ_KERNEL)
		allowed_macros = kernel_macros;

	add_hook(&match_condition, CONDITION_HOOK);
}
