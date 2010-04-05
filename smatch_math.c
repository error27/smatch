/*
 * sparse/smatch_helper.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static long long _get_implied_value(struct expression *expr, int *discard, int *undefined, int implied);
static long long _get_value(struct expression *expr, int *discard, int *undefined, int implied);

#define BOGUS 12345

#define NOTIMPLIED 0
#define IMPLIED 1
#define FUZZYMAX 2
#define FUZZYMIN 3

#define VAL_SINGLE 0
#define VAL_MAX    1
#define VAL_MIN    2

static long long cast_to_type(struct expression *expr, long long val)
{
	struct symbol *type = get_type(expr);

	if (!type)
		return val;

	switch (type->bit_size) {
	case 8:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned char) val;
		else
			val = (long long)(char) val;
		break;
	case 16:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned short) val;
		else
			val = (long long)(short) val;
		break;
	case 32:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned int) val;
		else
			val = (long long)(int) val;
		break;
	}
	return val;
}

static long long handle_preop(struct expression *expr, int *discard, int *undefined, int implied)
{
	long long ret = BOGUS;

	switch(expr->op) {
	case '~':
		ret = ~ _get_value(expr->unop, discard, undefined, implied);
		ret = cast_to_type(expr->unop, ret);
		break;
	case '-':
		ret = - _get_value(expr->unop, discard, undefined, implied);
		break;
	case '*':
		ret = _get_implied_value(expr, discard, undefined, implied);
		break;
	default:
		*undefined = 1;
		*discard = 1;
	}
	return ret;
}

static long long handle_binop(struct expression *expr, int *discard, int *undefined, int implied)
{
	long long left;
	long long right;
	long long ret = BOGUS;

	if (expr->type != EXPR_BINOP) {
		*undefined = 1;
		*discard = 1;
		return ret;
	}

	left = _get_value(expr->left, discard, undefined, implied);
	right = _get_value(expr->right, discard, undefined, implied);

	switch (expr->op) {
	case '*':
		ret =  left * right;
		break;
	case '/':
		if (right == 0) {
			*undefined = 1;
			*discard = 1;
		} else {
			ret = left / right;
		}
		break;
	case '+':
		ret = left + right;
		break;
	case '-':
		ret = left - right;
		break;
	case '%':
		if (right == 0) {
			*undefined = 1;
			*discard = 1;
		} else {
			ret = left % right;
		}
		break;
	case '|':
		ret = left | right;
		break;
	case '&':
		ret = left & right;
		break;
	case SPECIAL_RIGHTSHIFT:
		ret = left >> right;
		break;
	case SPECIAL_LEFTSHIFT:
		ret = left << right;
		break;
	case '^':
		ret = left ^ right;
		break;
	default:
		*undefined = 1;
		*discard = 1;
	}
	return ret;
}

static int get_implied_value_helper(struct expression *expr, long long *val, int what)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;
	
	if (get_value(expr, val))
		return 1;

	name = get_variable_from_expr(expr, &sym);
	if (!name)
		return 0;
	state = get_state(SMATCH_EXTRA, name, sym);
	free_string(name);
	if (!state || !state->data)
		return 0;
	if (what == VAL_SINGLE)
		return get_single_value_from_dinfo(get_dinfo(state), val);
	if (what == VAL_MAX) {
		*val = get_dinfo_max(get_dinfo(state));
		if (*val == whole_range.max) /* this means just guessing */
			return 0;
		return 1;
	}
        *val = get_dinfo_min(get_dinfo(state));
	if (*val == whole_range.min)
		return 0;
	return 1;
}

static int get_implied_single_fuzzy_max(struct expression *expr, long long *max)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	if (get_implied_max(expr, max))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	*max = whole_range.min;
	FOR_EACH_PTR(sm->possible, tmp) {
		long long new_min;

		new_min = get_dinfo_min(get_dinfo(tmp->state));
		if (new_min > *max)
			*max = new_min;
	} END_FOR_EACH_PTR(tmp);

	if (*max > whole_range.min)
		return 1;
	return 0;
}

static int get_implied_single_fuzzy_min(struct expression *expr, long long *min)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	if (get_implied_min(expr, min))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	*min = whole_range.max;
	FOR_EACH_PTR(sm->possible, tmp) {
		long long new_max;

		new_max = get_dinfo_max(get_dinfo(tmp->state));
		if (new_max < *min)
			*min = new_max;
	} END_FOR_EACH_PTR(tmp);

	if (*min < whole_range.max)
		return 1;
	return 0;
}

static long long _get_implied_value(struct expression *expr, int *discard, int *undefined, int implied)
{
	long long ret = BOGUS;

	switch (implied) {
	case IMPLIED:
		if (!get_implied_value_helper(expr, &ret, VAL_SINGLE)) {
			*undefined = 1;
			*discard = 1;
		}
		break;
	case FUZZYMAX:
		if (!get_implied_single_fuzzy_max(expr, &ret)) {
			*undefined = 1;
			*discard = 1;
		}
		break;
	case FUZZYMIN:
		if (!get_implied_single_fuzzy_min(expr, &ret)) {
			*undefined = 1;
			*discard = 1;
		}
		break;
	default:
		*undefined = 1;
		*discard = 1;
	}
	return ret;
}

static int get_const_value(struct expression *expr, long long *val)
{
	struct symbol *sym;

	sym = expr->symbol;
	if (!sym)
		return 0;
	if (!(sym->ctype.modifiers & MOD_CONST))
		return 0;
	if (get_value(sym->initializer, val))
		return 1;
	return 0;
}

static long long _get_value(struct expression *expr, int *discard, int *undefined, int implied)
{
	int dis = 0;
	long long ret = BOGUS;

	if (!expr) {
		*undefined = 1;
		return BOGUS;
	}
	if (!discard)
		discard = &dis;
	if (*discard) {
		*undefined = 1;
		return BOGUS;
	}
	
	expr = strip_parens(expr);

 	switch (expr->type){
	case EXPR_VALUE:
		ret = expr->value;
		ret = cast_to_type(expr, ret);
		break;
	case EXPR_PREOP:
		ret = handle_preop(expr, discard, undefined, implied);
		break;
	case EXPR_POSTOP:
		ret = _get_value(expr->unop, discard, undefined, implied);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		ret = _get_value(expr->cast_expression, discard, undefined, implied);
		return cast_to_type(expr, ret);
	case EXPR_BINOP:
		ret = handle_binop(expr, discard, undefined, implied);
		break;
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		ret = get_expression_value(expr);
		break;
	case EXPR_SYMBOL:
		if (get_const_value(expr, &ret))
			break;
	default:
		ret = _get_implied_value(expr, discard, undefined, implied);
	}
	if (*discard) {
		*undefined = 1;
		return BOGUS;
	}
	return ret;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value(struct expression *expr, long long *val)
{
	int undefined = 0;
	
	*val = _get_value(expr, NULL, &undefined, NOTIMPLIED);
	if (undefined)
		return 0;
	return 1;
}

int get_implied_max(struct expression *expr, long long *val)
{
	return get_implied_value_helper(expr, val, VAL_MAX);
}

int get_implied_min(struct expression *expr, long long *val)
{
	return get_implied_value_helper(expr, val, VAL_MIN);
}

int get_implied_value(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, NULL, &undefined, IMPLIED);
	return !undefined;
}

int get_fuzzy_max(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, NULL, &undefined, FUZZYMAX);
	return !undefined;
}

int get_fuzzy_min(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, NULL, &undefined, FUZZYMIN);
	return !undefined;
}
