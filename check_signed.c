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
	sval_t sval;
	sval_t max;
	sval_t min;
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
	if (!get_implied_value_sval(expr->right, &sval))
		return;
	max = sval_type_max(sym);
	if (sval_cmp(max, sval) < 0 && !(sval.value < 256 && max.value == 127)) {
		name = get_variable_from_expr_complex(expr->left, NULL);
		sm_msg("warn: value %s can't fit into %s '%s'",
		       sval_to_str(sval), sval_to_str(max), name);
		free_string(name);
	}
	min = sval_type_min(sym);
	if (sval_cmp_t(&llong_ctype, min, sval) > 0) {
		if (min.value == 0 && sval.value == -1) /* assigning -1 to unsigned variables is idiomatic */
			return;
		if (expr->right->type == EXPR_PREOP && expr->right->op == '~')
			return;
		if (expr->op == SPECIAL_SUB_ASSIGN || expr->op == SPECIAL_ADD_ASSIGN)
			return;
		name = get_variable_from_expr_complex(expr->left, NULL);
		if (min.value == 0) {
			sm_msg("warn: assigning %s to unsigned variable '%s'",
			       sval_to_str(sval), name);
		} else {
			sm_msg("warn: value %s can't fit into %s '%s'",
			       sval_to_str(sval), sval_to_str(min), name);
		}
		free_string(name);
	}
}

static const char *get_tf(sval_t variable, sval_t known, int var_pos, int op)
{
	if (op == SPECIAL_EQUAL)
		return "false";
	if (op == SPECIAL_NOTEQUAL)
		return "true";
	if (var_pos == VAR_ON_LEFT) {
		if (sval_cmp(variable, known) > 0 && (op == '<' || op == SPECIAL_LTE))
			return "false";
		if (sval_cmp(variable, known) > 0 && (op == '>' || op == SPECIAL_GTE))
			return "true";
		if (sval_cmp(variable, known) < 0 && (op == '<' || op == SPECIAL_LTE))
			return "true";
		if (sval_cmp(variable, known) < 0 && (op == '>' || op == SPECIAL_GTE))
			return "false";
	}
	if (var_pos == VAR_ON_RIGHT) {
		if (sval_cmp(known, variable) > 0 && (op == '<' || op == SPECIAL_LTE))
			return "false";
		if (sval_cmp(known, variable) > 0 && (op == '>' || op == SPECIAL_GTE))
			return "true";
		if (sval_cmp(known, variable) < 0 && (op == '<' || op == SPECIAL_LTE))
			return "true";
		if (sval_cmp(known, variable) < 0 && (op == '>' || op == SPECIAL_GTE))
			return "false";
	}
	return "the same";
}

static int compare_against_macro(int lr, struct expression *expr)
{
	struct expression *known = expr->left;

	if (lr == VAR_ON_LEFT)
		known = expr->right;

	return !!get_macro_name(known->pos);
}

static int cap_both_size(int lr, struct expression *expr)
{

	struct expression *var = expr->left;
	struct expression *tmp;
	char *name1 = NULL;
	char *name2 = NULL;
	int ret = 0;
	int i;

	/* screw it.  I am writing this to mark yoda code as buggy.
	 * Valid comparisons between an unsigned and zero are:
	 * 1) inside a macro.
	 * 2) foo < LOWER_BOUND where LOWER_BOUND is a macro.
	 * 3) foo < 0 || foo > X in exactly this format.  No Yoda.
	 */

	if (lr != VAR_ON_LEFT)
		return 0;
	if (expr->op != '<' && expr->op != SPECIAL_UNSIGNED_LT)
		return 0;

	i = 0;
	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (!i++)
			continue;
		if (tmp->op == SPECIAL_LOGICAL_OR) {
			struct expression *right = strip_expr(tmp->right);

			if (right->op != '>' &&
			    right->op != SPECIAL_UNSIGNED_GT &&
			    right->op != SPECIAL_GTE &&
			    right->op != SPECIAL_UNSIGNED_GTE)
				return 0;

			name1 = get_variable_from_expr_complex(var, NULL);
			if (!name1)
				goto free;

			name2 = get_variable_from_expr_complex(right->left, NULL);
			if (!name2)
				goto free;
			if (!strcmp(name1, name2))
				ret = 1;
			goto free;

		}
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);

free:
	free_string(name1);
	free_string(name2);
	return ret;
}

static void match_condition(struct expression *expr)
{
	struct expression *var = NULL;
	struct symbol *var_type = NULL;
	struct symbol *known_type = NULL;
	sval_t known;
	sval_t max;
	sval_t min;
	int lr;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;

	if (get_value_sval(expr->left, &known)) {
		if (get_value_sval(expr->right, &max))
			return; /* both sides known */
		lr = VAR_ON_RIGHT;
		var = expr->right;
		known_type = get_type(expr->left);
	} else if (get_value_sval(expr->right, &known)) {
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
		sval_t sval_min, sval_max;

		sval_min = sval_type_min(var_type);
		sval_max = sval_type_max(var_type);

		if (sval_cmp(sval_min, known) > 0 || sval_cmp(sval_max, known) < 0)
			sm_msg("error: %s is never equal to %s (wrong type %s - %s).",
				name, sval_to_str(known), sval_to_str(sval_min), sval_to_str(sval_max));
		goto free;
	}

	max = sval_type_max(var_type);
	min = sval_type_min(var_type);

	if (sval_cmp(max, known) < 0) {
		const char *tf = get_tf(max, known, lr, expr->op);

		sm_msg("warn: %s is more than %s (max '%s' can be) so this is always %s.",
			sval_to_str(known), sval_to_str(max), name, tf);
	}

	if (known.value == 0 && type_unsigned(var_type)) {
		if ((lr && expr->op == '<') ||
		    (lr && expr->op == SPECIAL_UNSIGNED_LT) ||
		    (!lr && expr->op == '>') ||
		    (!lr && expr->op == SPECIAL_UNSIGNED_GT)) {
			if (!compare_against_macro(lr, expr) && !cap_both_size(lr, expr)) {
				sm_msg("warn: unsigned '%s' is never less than zero.", name);
				goto free;
			}
		}
	}

	if (type_unsigned(var_type) && known_type && !type_unsigned(known_type) && known.value < 0) {
		sm_msg("warn: unsigned '%s' is never less than zero %s.", name, sval_to_str(known));
		goto free;
	}

	if (sval_signed(min) && sval_signed(known) && sval_cmp(min, known) > 0) {
		const char *tf = get_tf(min, known, lr, expr->op);

		sm_msg("warn: %s is less than %s (min '%s' can be) so this is always %s.",
			sval_to_str(known), sval_to_str(min), name, tf);
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
