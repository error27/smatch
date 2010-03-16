/*
 * sparse/smatch_helper.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static long long _get_value(struct expression *expr, int *discard, int *undefined, int implied);

#define BOGUS 12345

#define NOTIMPLIED 0
#define IMPLIED 1
#define FUZZYMAX 2
#define FUZZYMIN 3

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
	default:
		*undefined = 1;
		*discard = 1;
	}
	return ret;
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
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
	{
		struct symbol *type = get_base_type(expr->cast_type);

		ret = _get_value(expr->cast_expression, discard, undefined, implied);
		switch (type->bit_size) {
		case 8:
			if (type->ctype.modifiers & MOD_UNSIGNED)
				ret = (long long)(unsigned char) ret;
			else
				ret = (long long)(char) ret;
			break;
		case 16:
			if (type->ctype.modifiers & MOD_UNSIGNED)
				ret = (long long)(unsigned short) ret;
			else
				ret = (long long)(short) ret;
			break;
		case 32:
			if (type->ctype.modifiers & MOD_UNSIGNED)
				ret = (long long)(unsigned int) ret;
			else
				ret = (long long)(int) ret;
			break;
		}
		return ret;
	}
	case EXPR_BINOP: {
		long long left;
		long long right;

		left = _get_value(expr->left, discard, undefined, implied);
		right = _get_value(expr->right, discard, undefined, implied);
		if (expr->op == '*') {
			ret =  left * right;
		} else if (expr->op == '/') {
			ret = left / right;
		} else if (expr->op == '+') {
			ret = left + right;
		} else if (expr->op == '-') {
			ret = left - right;
		} else if (expr->op == '|') {
			ret = left | right;
		} else if (expr->op == '&') {
			ret = left & right;
		} else if (expr->op == SPECIAL_RIGHTSHIFT) {
			ret = left >> right;
		} else if (expr->op == SPECIAL_LEFTSHIFT) {
			ret = left << right;
		} else if (expr->op == '^') {
			ret = left ^ right;
		} else {
			*undefined = 1;
			*discard = 1;
		}
		break;
	}
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		ret = get_expression_value(expr);
		break;
	default:
		switch (implied) {
		case IMPLIED:
			if (!get_implied_single_val(expr, &ret)) {
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
