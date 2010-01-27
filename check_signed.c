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

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	long long val;
	long long max;
	long long min;
	char *name;

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
	if (max && max < val) {
		name = get_variable_from_expr_complex(expr->left, NULL);
		sm_msg("warn: value %lld can't fit into %lld '%s'", val, max, name);
		free_string(name);
	}
	min = type_min(sym);
	if (min > val) {
		if (min == 0 && val == -1) /* assigning -1 to unsigned variables is idiomatic */
			return; 
		name = get_variable_from_expr_complex(expr->left, NULL);
		if (min == 0)
			sm_msg("warn: assigning %lld to unsigned variable '%s'", val, name);
		else
			sm_msg("warn: value %lld can't fit into %lld '%s'", val, min, name);
		free_string(name);
	}

}

static const char *get_tf(long long variable, long long known, int var_pos, char op)
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
	struct symbol *type = NULL;
	long long max;
	long long min;
	int lr;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;

	if (get_value(expr->left, &known)) {
		lr = VAR_ON_RIGHT;
		var = expr->right;
	} else if (get_value(expr->right, &known)) {
		lr = VAR_ON_LEFT;
		var = expr->left;
	} else {
		return;
	}

	type = get_type(var);
	if (!type || type->bit_size >= 32)
		return;

	max = type_max(type);
	if (!max)
		return;
	min = type_min(type);

	name = get_variable_from_expr_complex(var, NULL);

	if (known < 0 && type_unsigned(type)) {
		sm_msg("error: comparing unsigned '%s' to negative", name);
		goto free;
	}

	if (known == 0) {
		if (!type_unsigned(type))
			goto free;
		if (lr == VAR_ON_LEFT) {
			if (expr->op == '<')
				sm_msg("error: unsigned '%s' cannot be less than 0", name);
			if (expr->op == SPECIAL_LTE)
				sm_msg("warn: unsigned '%s' cannot be less than 0", name);
		}
		if (lr == VAR_ON_RIGHT) {
			if (expr->op == '>')
				sm_msg("error: unsigned '%s' cannot be less than 0", name);
			if (expr->op == SPECIAL_GTE)
				sm_msg("warn: unsigned '%s' cannot be less than 0", name);
		}
		goto free;
	}

	if (max < known) {
		const char *tf = get_tf(max, known, lr, expr->op);

		sm_msg("warn: %lld is more than %lld (max '%s' can be) so this is always %s.",
			known, max, name, tf);
	}

	if (min > known) {
		const char *tf = get_tf(max, known, lr, expr->op);

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
