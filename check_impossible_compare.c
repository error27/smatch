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

static const char *okay_macros[] = {
	"ULONG_MAX",
};

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

static bool is_lt_zero(int op, sval_t known)
{
	if (op != '<' && op != SPECIAL_UNSIGNED_LT)
		return false;
	if (known.value != 0)
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

static bool is_long_vs_UINTMAX(struct range_list *rl, int op, sval_t known)
{
	if (op != '>' && op != SPECIAL_UNSIGNED_GT)
		return false;
	if (rl_type(rl) != &long_ctype && rl_type(rl) != &ulong_ctype)
		return false;
	if (known.value == UINT_MAX)
		return true;
	return false;
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
	sval_t known, dummy;
	struct range_list *var_rl, *known_rl, *rl_right, *rl_left;
	char *name;
	int op;

	if (expr->type != EXPR_COMPARE)
		return;

	type = get_type(expr);
	if (!type)
		return;

	if (get_macro_name(expr->pos))
		return;

	op = expr->op;
	if (get_value(expr->right, &known)) {
		/* check that one and only one side is known */
		if (get_value(expr->left, &dummy))
			return;

		var_rl = alloc_whole_rl(get_type(expr->left));
	} else if (get_value(expr->left, &known)) {
		op = flip_comparison(op);
		var_rl = alloc_whole_rl(get_type(expr->right));
	} else {
		return;
	}
	known = sval_cast(type, known);
	known_rl = alloc_rl(known, known);
	var_rl = cast_rl(type, var_rl);

	/* handled by check_unsigned_lt_zero.c */
	if (is_lt_zero(op, known))
		return;

	if (possibly_true_rl(var_rl, op, known_rl))
		return;
	if (is_u64_vs_ulongmax(expr))
		return;
	if (is_long_vs_UINTMAX(var_rl, op, known))
		return;
	if (is_allowed_impossible_limit(expr->right))
		return;

	name = expr_to_str(expr);
	get_absolute_rl(expr->left, &rl_left);
	get_absolute_rl(expr->right, &rl_right);
	sm_warning("impossible condition '(%s) => (%s %s %s)'", name,
		   show_rl(rl_left), show_special(expr->op), show_rl(rl_right));
	free_string(name);
}

void check_impossible_compare(int id)
{
	my_id = id;

	allowed_macros = okay_macros;
	if (option_project == PROJ_KERNEL)
		allowed_macros = kernel_macros;

	add_hook(&match_condition, CONDITION_HOOK);
}
