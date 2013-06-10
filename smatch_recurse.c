/*
 * sparse/smatch_recurse.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

#define RECURSE_LIMIT 10

static int recurse(struct expression *expr,
		   int (func)(struct expression *expr, void *p),
		   void *param, int nr)
{
	int ret;

	if (!expr)
		return 0;

	ret = func(expr, param);
	if (ret)
		return ret;

	if (nr > RECURSE_LIMIT)
		return -1;
	nr++;

	switch (expr->type) {
	case EXPR_PREOP:
		ret = recurse(expr->unop, func, param, nr);
		break;
	case EXPR_POSTOP:
		ret = recurse(expr->unop, func, param, nr);
		break;
	case EXPR_STATEMENT:
		return -1;
		break;
	case EXPR_LOGICAL:
	case EXPR_COMPARE:
	case EXPR_BINOP:
	case EXPR_COMMA:
		ret = recurse(expr->left, func, param, nr);
		if (ret)
			return ret;
		ret = recurse(expr->right, func, param, nr);
		break;
	case EXPR_ASSIGNMENT:
		ret = recurse(expr->right, func, param, nr);
		if (ret)
			return ret;
		ret = recurse(expr->left, func, param, nr);
		break;
	case EXPR_DEREF:
		ret = recurse(expr->deref, func, param, nr);
		break;
	case EXPR_SLICE:
		ret = recurse(expr->base, func, param, nr);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
		ret = recurse(expr->cast_expression, func, param, nr);
		break;
	case EXPR_SIZEOF:
	case EXPR_OFFSETOF:
	case EXPR_ALIGNOF:
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		ret = recurse(expr->conditional, func, param, nr);
		if (ret)
			return ret;
		ret = recurse(expr->cond_true, func, param, nr);
		if (ret)
			return ret;
		ret = recurse(expr->cond_false, func, param, nr);
		break;
	case EXPR_CALL:
		return -1;
		break;
	case EXPR_INITIALIZER:
		return -1;
		break;
	case EXPR_IDENTIFIER:
		ret = recurse(expr->ident_expression, func, param, nr);
		break;
	case EXPR_INDEX:
		ret = recurse(expr->idx_expression, func, param, nr);
		break;
	case EXPR_POS:
		ret = recurse(expr->init_expr, func, param, nr);
		break;
	case EXPR_SYMBOL:
	case EXPR_STRING:
	case EXPR_VALUE:
		break;
	default:
		return -1;
		break;
	};
	return ret;
}

static int has_symbol_helper(struct expression *expr, void *_sym)
{
	struct symbol *sym = _sym;

	if (!expr || expr->type != EXPR_SYMBOL)
		return 0;
	if (expr->symbol == sym)
		return 1;
	return 0;
}

int has_symbol(struct expression *expr, struct symbol *sym)
{
	return recurse(expr, has_symbol_helper, sym, 0);
}

int has_variable(struct expression *expr, struct expression *var)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(var, &sym);
	free_string(name);
	if (!sym)
		return -1;
	return has_symbol(expr, sym);
}
