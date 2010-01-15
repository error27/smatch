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

#define RIGHT 0
#define LEFT 1

static unsigned long long max_size(struct symbol *base_type)
{
	/*
	  I wanted to say:

	  unsigned long long ret = 0xffffffffffffffff;

	  But gcc complained that was too large.  What am I doing wrong?
	  Oh well.  I expect most of the problems are with smaller 
	  values anyway 
	*/

	unsigned long long ret = 0xffffffff;
	int bits;

	bits = base_type->bit_size;
	if (base_type->ctype.modifiers & MOD_SIGNED)
		bits--;
	ret >>= (32 - bits);
	return ret;
}

static int is_unsigned(struct symbol *base_type)
{
	if (base_type->ctype.modifiers & MOD_UNSIGNED)
		return 1;
	return 0;
}

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	long long val;
	long long max;
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
	max = max_size(sym);
	if (max && max < val) {
		name = get_variable_from_expr_complex(expr->left, NULL);
		sm_msg("warn: value %lld can't fit into %lld '%s'", val, max, name);
		free_string(name);
	}
}

static void match_condition(struct expression *expr)
{
	long long known;
	struct expression *var = NULL;
	struct symbol *type = NULL;
	long long max;
	int lr;
	char *name;

	if (expr->type != EXPR_COMPARE)
		return;

	if (get_value(expr->left, &known)) {
		lr = RIGHT;
		var = expr->right;
	} else if (get_value(expr->right, &known)) {
		lr = LEFT;
		var = expr->left;
	} else {
		return;
	}

	type = get_type(var);
	if (!type || type->bit_size >= 32)
		return;

	max = max_size(type);
	if (!max)
		return;

	name = get_variable_from_expr_complex(var, NULL);

	if (known < 0) {
		if (is_unsigned(type))
			sm_msg("error: comparing '%s' to negative", name);
		goto free;
	}

	if (known == 0) {
		if (!is_unsigned(type))
			goto free;
		if (lr == LEFT) {
			if (expr->op == '<')
				sm_msg("error: unsigned '%s' cannot be less than 0", name);
			if (expr->op == SPECIAL_LTE)
				sm_msg("warn: unsigned '%s' cannot be less than 0", name);
		}
		if (lr == RIGHT) {
			if (expr->op == '>')
				sm_msg("error: unsigned '%s' cannot be less than 0", name);
			if (expr->op == SPECIAL_GTE)
				sm_msg("warn: unsigned '%s' cannot be less than 0", name);
		}
		goto free;
	}

	if (max < known) {
		const char *tf = "the same";

		if (expr->op == SPECIAL_EQUAL)
			tf = "false";
		if (expr->op == SPECIAL_NOTEQUAL)
			tf = "true";
		if (lr == LEFT && (expr->op == '<' || expr->op == SPECIAL_LTE))
			tf = "true";
		if (lr == RIGHT && (expr->op == '>' || expr->op == SPECIAL_GTE))
			tf = "false";
		sm_msg("warn: %lld is higher than %lld (max '%s' can be) so this is always %s.",
			known, max, name, tf);
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
