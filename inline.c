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

#define copy_one_statement(stmt) (stmt)
#define copy_expression(expr) (expr)

/*
 * Copy a stateemnt tree from 'src' to 'dst', where both
 * source and destination are of type STMT_COMPOUND.
 *
 * We do this for the tree-level inliner.
 *
 * This doesn't do the symbol replacements right, duh.
 */
void copy_statement(struct statement *src, struct statement *dst)
{
	struct statement *stmt;
	struct symbol *sym;

	FOR_EACH_PTR(src->syms, sym) {
		struct symbol *newsym = alloc_symbol(sym->pos, sym->type);
		newsym->ctype = sym->ctype;
		newsym->initializer = copy_expression(sym->initializer);
		add_symbol(&dst->syms, newsym);
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(src->stmts, stmt) {
		add_statement(&dst->stmts, copy_one_statement(stmt));
	} END_FOR_EACH_PTR;
}
