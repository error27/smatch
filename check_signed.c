/*
 * sparse/check_signed.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Check for things which are signed but probably should be unsigned.
 * 
 * Hm...  It seems like at this point in the processing, sparse makes all
 * bitfields unsigned.  Which is logical but not what GCC does.
 *
 */

#include "smatch.h"

static int my_id;

#define VAR_ON_RIGHT 0
#define VAR_ON_LEFT 1

static long long eqneq_max(struct symbol *base_type)
{
	long long ret = whole_range.max;
	int bits;

	if (!base_type || !base_type->bit_size)
		return ret;
	bits = base_type->bit_size;
	if (bits == 64)
		return ret;
	if (bits < 32)
		return type_max(base_type);
	ret >>= (63 - bits);
	return ret;
}

static long long eqneq_min(struct symbol *base_type)
{
	long long ret = whole_range.min;
	int bits;

	if (!base_type || !base_type->bit_size)
		return ret;
	if (base_type->bit_size < 32)
		return type_min(base_type);
	ret = whole_range.max;
	bits = base_type->bit_size - 1;
	ret >>= (63 - bits);
	return -(ret + 1);
}

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	long long val;
	long long max;
	long long min;
	char *name;

	if (expr->op == SPECIAL_AND_ASSIGN || expr->op == SPECIAL_OR_ASSIGN)
		return;

	sym = get_type(expr->left);
	if (!sym) {
		//sm_msg("could not get type");
		return;
	}
	if (sym->bit_size >= 32) /* max_val limits this */
		return;
	if (!get_implied_value(expr->right, &val))
		return;
	max = type_max(sym);
	if (max < val && !(val < 256 && max == 127)) {
		name = get_variable_from_expr_complex(expr->left, NULL);
		sm_msg("warn: value %lld can't fit into %lld '%s'", val, max, name);
		free_string(name);
	}
	min = type_min(sym);
	if (min > val) {
		if (min == 0 && val == -1) /* assigning -1 to unsigned variables is idiomatic */
			return;
		if (expr->right->type == EXPR_PREOP && expr->right->op == '~')
			return;
		if (expr->op == SPECIAL_SUB_ASSIGN || expr->op == SPECIAL_ADD_ASSIGN)
			return;
		name = get_variable_from_expr_complex(expr->left, NULL);
		if (min == 0)
			sm_msg("warn: assigning %lld to unsigned variable '%s'", val, name);
		else
			sm_msg("warn: value %lld can't fit into %lld '%s'", val, min, name);
		free_string(name);
	}

}

static const char *get_tf(long long variable, long long known, int var_pos, int op)
{
	if (op == SPECIAL_EQUAL)
		return "false";
	if (op == SPECIAL_NOTEQUAL)
		return "true";
	if (var_pos == VAR_ON_LEFT) {
		if (variable > known && (op == '<' || op == SPECIAL_LTE))
			return "false";
		if (variable > known && (op == '>' || op == SPECIAL_GTE))
			return "true";
		if (variable < known && (op == '<' || op == SPECIAL_LTE))
			return "true";
		if (variable < known && (op == '>' || op == SPECIAL_GTE))
			return "false";
	}
	if (var_pos == VAR_ON_RIGHT) {
		if (known > variable && (op == '<' || op == SPECIAL_LTE))
			return "false";
		if (known > variable && (op == '>' || op == SPECIAL_GTE))
			return "true";
		if (known < variable && (op == '<' || op == SPECIAL_LTE))
			return "true";
		if (known < variable && (op == '>' || op == SPECIAL_GTE))
			return "false";
	}
	return "the same";
}

static void match_condition(struct expression *expr)
{
	long long known;
	struct expression *var = NULL;
	struct symbol *var_type = NULL;
	struct symbol *known_type = NULL;
	long long max;
	long long min;
	int lr;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;

	if (get_value(expr->left, &known)) {
		if (get_value(expr->right, &max))
			return; /* both sides known */
		lr = VAR_ON_RIGHT;
		var = expr->right;
		known_type = get_type(expr->left);
	} else if (get_value(expr->right, &known)) {
		lr = VAR_ON_LEFT;
		var = expr->left;
		known_type = get_type(expr->right);
	} else {
		return;
	}

	var_type = get_type(var);
	if (!var_type)
		return;
	if (var_type->bit_size >= 32 && !option_spammy)
		return;

	name = get_variable_from_expr_complex(var, NULL);

	if (expr->op == SPECIAL_EQUAL || expr->op == SPECIAL_NOTEQUAL) {
		if (eqneq_max(var_type) < known || eqneq_min(var_type) > known)
			sm_msg("error: %s is never equal to %lld (wrong type %lld - %lld).",
				name, known, eqneq_min(var_type), eqneq_max(var_type));
		goto free;
	}

	max = type_max(var_type);
	min = type_min(var_type);

	if (max < known) {
		const char *tf = get_tf(max, known, lr, expr->op);

		sm_msg("warn: %lld is more than %lld (max '%s' can be) so this is always %s.",
			known, max, name, tf);
	}

	if (known == 0 && type_unsigned(var_type)) {
		if ((lr && expr->op == '<') || (!lr && expr->op == '>'))
			sm_msg("warn: unsigned '%s' is never less than zero.", name);
		goto free;
	}

	if (type_unsigned(var_type) && known_type && !type_unsigned(known_type) && known < 0) {
		sm_msg("warn: unsigned '%s' is never less than zero (%lld).", name, known);
		goto free;
	}

	if (min < 0 && min > known) {
		const char *tf = get_tf(min, known, lr, expr->op);

		sm_msg("warn: %lld is less than %lld (min '%s' can be) so this is always %s.",
			known, min, name, tf);
	}
free:
	free_string(name);
}

void check_signed(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
}
