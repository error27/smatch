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

static struct expression * dup_expression(struct expression *expr)
{
	struct expression *dup = alloc_expression(expr->pos, expr->type);
	*dup = *expr;
	return dup;
}

static struct statement * dup_statement(struct statement *stmt)
{
	struct statement *dup = alloc_statement(stmt->pos, stmt->type);
	*dup = *stmt;
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

	/* Statement expression */
	case EXPR_STATEMENT: {
		struct statement *stmt = alloc_statement(expr->pos, STMT_COMPOUND);
		copy_statement(expr->statement, stmt);
		expr = dup_expression(expr);
		expr->statement = stmt;
		break;
	}

	/* Call expression */
	case EXPR_CALL: {
		struct expression *fn = copy_expression(expr->fn);
		struct expression_list *list = expr->args;
		struct expression *arg;

		expr = dup_expression(expr);
		expr->fn = fn;
		expr->args = NULL;
		FOR_EACH_PTR(list, arg) {
			add_expression(&expr->args, copy_expression(arg));
		} END_FOR_EACH_PTR;
		break;
	}

	/* Initializer list statement */
	case EXPR_INITIALIZER: {
		struct expression_list *list = expr->expr_list;
		struct expression *entry;
		expr = dup_expression(expr);
		expr->expr_list = NULL;
		FOR_EACH_PTR(list, entry) {
			add_expression(&expr->expr_list, copy_expression(entry));
		} END_FOR_EACH_PTR;
		break;
	}

	default:
		if (verbose)
			warn(expr->pos, "trying to copy expression type %d", expr->type);
	}
	return expr;
}

void set_replace(struct symbol *old, struct symbol *new)
{
	new->replace = old;
	old->replace = new;
}

void unset_replace(struct symbol *sym)
{
	struct symbol *r = sym->replace;
	if (r) {
		r->replace = NULL;
		sym->replace = NULL;
	}
}

struct symbol *copy_symbol(struct symbol *orig)
{
	struct symbol *sym = orig;
	if (orig) {
		sym = alloc_symbol(orig->pos, orig->type);
		sym->ctype = orig->ctype;
		sym->initializer = copy_expression(orig->initializer);
		set_replace(orig, sym);
	}
	return orig;
}

struct symbol_list *copy_symbol_list(struct symbol_list *src)
{
	struct symbol_list *dst = NULL;
	struct symbol *sym;

	FOR_EACH_PTR(src, sym) {
		struct symbol *newsym = copy_symbol(sym);
		add_symbol(&dst, newsym);
	} END_FOR_EACH_PTR;
	return dst;
}

static struct statement *copy_one_statement(struct statement *stmt)
{
	if (!stmt)
		return NULL;
	switch(stmt->type) {
	case STMT_NONE:
		break;
	case STMT_EXPRESSION: {
		struct expression *expr = copy_expression(stmt->expression);
		if (expr == stmt->expression)
			break;
		stmt = dup_statement(stmt);
		stmt->expression = expr;
		break;
	}
	case STMT_COMPOUND: {
		struct statement *new = alloc_statement(stmt->pos, STMT_COMPOUND);
		copy_statement(stmt, new);
		stmt = new;
		break;
	}
	case STMT_IF: {
		struct expression *cond = stmt->if_conditional;
		struct statement *true = stmt->if_true;
		struct statement *false = stmt->if_false;

		cond = copy_expression(cond);
		true = copy_one_statement(true);
		false = copy_one_statement(false);
		if (stmt->if_conditional == cond &&
		    stmt->if_true == true &&
		    stmt->if_false == false)
			break;
		stmt = dup_statement(stmt);
		stmt->if_conditional = cond;
		stmt->if_true = true;
		stmt->if_false = false;
		break;
	}
	case STMT_RETURN: {
		struct expression *retval = copy_expression(stmt->ret_value);
		struct symbol *sym = stmt->ret_target;

		if (sym && sym->replace)
			sym = sym->replace;
		stmt = dup_statement(stmt);
		stmt->ret_value = retval;
		stmt->ret_target = sym;
		break;
	}
	case STMT_CASE: {
		stmt = dup_statement(stmt);
		stmt->case_label = copy_symbol(stmt->case_label);
		stmt->case_expression = copy_expression(stmt->case_expression);
		stmt->case_to = copy_expression(stmt->case_to);
		stmt->case_statement = copy_one_statement(stmt->case_statement);
		break;
	}
	case STMT_SWITCH: {
		struct symbol *switch_break = copy_symbol(stmt->switch_break);
		struct symbol *switch_case = copy_symbol(stmt->switch_case);
		struct expression *expr = copy_expression(stmt->switch_expression);
		struct statement *switch_stmt = copy_one_statement(stmt->switch_statement);
		stmt = dup_statement(stmt);
		stmt->switch_break = switch_break;
		stmt->switch_case = switch_case;
		stmt->switch_expression = expr;
		stmt->switch_statement = switch_stmt;
		break;		
	}
	case STMT_ITERATOR: {
		stmt = dup_statement(stmt);
		stmt->iterator_break = copy_symbol(stmt->iterator_break);
		stmt->iterator_continue = copy_symbol(stmt->iterator_continue);
		stmt->iterator_syms = copy_symbol_list(stmt->iterator_syms);

		stmt->iterator_pre_statement = copy_one_statement(stmt->iterator_pre_statement);
		stmt->iterator_pre_condition = copy_expression(stmt->iterator_pre_condition);

		stmt->iterator_statement = copy_one_statement(stmt->iterator_statement);

		stmt->iterator_post_statement = copy_one_statement(stmt->iterator_post_statement);
		stmt->iterator_post_condition = copy_expression(stmt->iterator_post_condition);
		break;
	}
	case STMT_LABEL: {
		stmt = dup_statement(stmt);
		stmt->label_identifier = copy_symbol(stmt->label_identifier);
		stmt->label_statement = copy_one_statement(stmt->label_statement);
		break;
	}
	case STMT_GOTO: {
		/* FIXME! */
		break;
	}
	case STMT_ASM: {
		/* FIXME! */
		break;
	}
	default:
		if (verbose)
			warn(stmt->pos, "trying to copy statement type %d", stmt->type);
		break;
	}
	return stmt;
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
		newsym->ctype = sym->ctype;
		newsym->initializer = copy_expression(sym->initializer);
		add_symbol(&dst->syms, newsym);

		set_replace(sym, newsym);
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(src->stmts, stmt) {
		add_statement(&dst->stmts, copy_one_statement(stmt));
	} END_FOR_EACH_PTR;

	FOR_EACH_PTR(dst->syms, sym) {
		unset_replace(sym);
	} END_FOR_EACH_PTR;
}

int inline_function(struct expression *expr, struct symbol *sym)
{
	struct symbol *fn = sym->ctype.base_type;
	struct expression_list *arg_list = expr->args;
	struct statement *stmt = alloc_statement(expr->pos, STMT_COMPOUND);
	struct symbol_list *name_list = sym->ctype.base_type->arguments;
	struct symbol *name;
	struct expression *arg;

	if (!fn->stmt) {
		warn(fn->pos, "marked inline, but without a definition");
		return 0;
	}

	stmt = alloc_statement(expr->pos, STMT_COMPOUND);

	expr->type = EXPR_STATEMENT;
	expr->statement = stmt;
	expr->ctype = fn->ctype.base_type;

	/*
	 * FIXME! On expansion, we'll need to replace the original return
	 * symbol with this local one!
	 */
	stmt->ret = alloc_symbol(sym->pos, SYM_NODE);

	/*
	 * FIXME! On expansion, we'll also need to replace the original
	 * argument symbols with these!
	 */
	PREPARE_PTR_LIST(name_list, name);
	FOR_EACH_PTR(arg_list, arg) {
		struct symbol *a = alloc_symbol(arg->pos, SYM_NODE);

		a->ctype.base_type = arg->ctype;
		if (name) {
			a->ident = name->ident;
			a->ctype = name->ctype;
			set_replace(name, a);
		}
		a->initializer = arg;
		add_symbol(&stmt->syms, a);

		NEXT_PTR_LIST(name);
	} END_FOR_EACH_PTR;
	FINISH_PTR_LIST(name);

	copy_statement(fn->stmt, stmt);
	evaluate_statement(stmt);

	return 1;
}
