/*
 * Sparse - a semantic source parser.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *
 *  Licensed under the Open Software License version 1.1
 */

#include <stdlib.h>
#include <stdio.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"

#define copy_one_statement(stmt) (stmt)

static struct expression * dup_expression(struct expression *expr)
{
	struct expression *dup = alloc_expression(expr->pos, expr->type);
	*dup = *expr;
	return dup;
}

static struct expression * copy_expression(struct expression *expr)
{
	if (!expr)
		return NULL;

	switch (expr->type) {
	/*
	 * EXPR_SYMBOL is the interesting case, we may need to replace the
	 * symbol to the new copy.
	 */
	case EXPR_SYMBOL: {
		struct symbol *sym = expr->symbol->replace;
		if (!sym)
			break;
		expr = dup_expression(expr);
		expr->symbol = sym;
		break;
	}

	/* Atomics, never change, just return the expression directly */
	case EXPR_VALUE:
	case EXPR_STRING:
		break;

	/* Unops: check if the subexpression is unique */
	case EXPR_PREOP:
	case EXPR_POSTOP: {
		struct expression *unop = copy_expression(expr->unop);
		if (expr->unop == unop)
			break;
		expr = dup_expression(expr);
		expr->unop = unop;
		break;
	}

	/* Binops: copy left/right expressions */
	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT: {
		struct expression *left = copy_expression(expr->left);
		struct expression *right = copy_expression(expr->right);
		if (left == expr->left && right == expr->right)
			break;
		expr = dup_expression(expr);
		expr->left = left;
		expr->right = right;
		break;
	}

	/* Dereference */
	case EXPR_DEREF: {
		struct expression *deref = copy_expression(expr->deref);
		if (deref == expr->deref)
			break;
		expr = dup_expression(expr);
		expr->deref = deref;
		break;
	}

	/* Cast/sizeof */
	case EXPR_CAST:
	case EXPR_SIZEOF: {
		struct expression *cast = copy_expression(expr->cast_expression);
		if (cast == expr->cast_expression)
			break;
		expr = dup_expression(expr);
		expr->cast_expression = cast;
		break;
	}

	/* Conditional expression */
	case EXPR_CONDITIONAL: {
		struct expression *cond = copy_expression(expr->conditional);
		struct expression *true = copy_expression(expr->cond_true);
		struct expression *false = copy_expression(expr->cond_false);
		if (cond == expr->conditional && true == expr->cond_true && false == expr->cond_false)
			break;
		expr = dup_expression(expr);
		expr->conditional = cond;
		expr->cond_true = true;
		expr->cond_false = false;
		break;
	}	

	default:
		warn(expr->pos, "trying to copy expression type %d", expr->type);
	}
	return expr;
}

/*
 * Copy a stateemnt tree from 'src' to 'dst', where both
 * source and destination are of type STMT_COMPOUND.
 *
 * We do this for the tree-level inliner.
 *
 * This doesn't do the symbol replacement right: it's not
 * re-entrant.
 */
void copy_statement(struct statement *src, struct statement *dst)
{
	struct statement *stmt;
	struct symbol *sym;

	FOR_EACH_PTR(src->syms, sym) {
		struct symbol *newsym = alloc_symbol(sym->pos, sym->type);
		newsym->replace = sym;
		sym->replace = newsym;
		newsym->ctype = sym->ctype;
		newsym->initializer = copy_expression(sym->initializer);
		add_symbol(&dst->syms, newsym);
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(src->stmts, stmt) {
		add_statement(&dst->stmts, copy_one_statement(stmt));
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(src->syms, sym) {
		sym->replace = NULL;
	} END_FOR_EACH_PTR;
}
