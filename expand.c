/*
 * sparse/expand.c
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 *
 * expand constant expressions.
 */
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include "lib.h"
#include "parse.h"
#include "token.h"
#include "symbol.h"
#include "target.h"
#include "expression.h"

static void expand_expression(struct expression *);
static void expand_statement(struct statement *);

static void expand_symbol_expression(struct expression *expr)
{
	struct symbol *sym = expr->symbol;

	/*
	 * The preprocessor can cause unknown symbols to be generated
	 */
	if (!sym) {
		warn(expr->pos, "undefined preprocessor identifier '%s'", show_ident(expr->symbol_name));
		expr->type = EXPR_VALUE;
		expr->value = 0;
		return;
	}
}

void cast_value(struct expression *expr, struct symbol *newtype,
		struct expression *old, struct symbol *oldtype)
{
	int old_size = oldtype->bit_size;
	int new_size = newtype->bit_size;
	long long value, mask, ormask, andmask;
	int is_signed;

	// FIXME! We don't handle FP casts of constant values yet
	if (newtype->ctype.base_type == &fp_type)
		return;
	if (oldtype->ctype.base_type == &fp_type)
		return;

	// For pointers and integers, we can just move the value around
	expr->type = EXPR_VALUE;
	if (old_size == new_size) {
		expr->value = old->value;
		return;
	}

	// expand it to the full "long long" value
	is_signed = !(oldtype->ctype.modifiers & MOD_UNSIGNED);
	mask = 1ULL << (old_size-1);
	value = old->value;
	if (!(value & mask))
		is_signed = 0;
	andmask = mask | (mask-1);
	ormask = ~andmask;
	if (!is_signed)
		ormask = 0;
	value = (value & andmask) | ormask;

	// Truncate it to the new size
	mask = 1ULL << (new_size-1);
	mask = mask | (mask-1);
	expr->value = value & mask;
}

static int check_shift_count(struct expression *expr, struct symbol *ctype, unsigned int count)
{
	if (count >= ctype->bit_size) {
		warn(expr->pos, "shift too big for type (%x)", ctype->ctype.modifiers);
		count &= ctype->bit_size-1;
	}
	return count;
}

/*
 * CAREFUL! We need to get the size and sign of the
 * result right!
 */
static void simplify_int_binop(struct expression *expr, struct symbol *ctype)
{
	struct expression *left = expr->left, *right = expr->right;
	unsigned long long v, l, r, mask;
	signed long long s, sl, sr;
	int is_signed, shift;

	if (left->type != EXPR_VALUE || right->type != EXPR_VALUE)
		return;
	l = left->value; r = right->value;
	is_signed = !(ctype->ctype.modifiers & MOD_UNSIGNED);
	mask = 1ULL << (ctype->bit_size-1);
	sl = l; sr = r;
	if (is_signed && (sl & mask))
		sl |= ~(mask-1);
	if (is_signed && (sr & mask))
		sr |= ~(mask-1);
	
	switch (expr->op) {
	case '+':		v = l + r; s = v; break;
	case '-':		v = l - r; s = v; break;
	case '&':		v = l & r; s = v; break;
	case '|':		v = l | r; s = v; break;
	case '^':		v = l ^ r; s = v; break;
	case '*':		v = l * r; s = sl * sr; break;
	case '/':		if (!r) return; v = l / r; s = sl / sr; break;
	case '%':		if (!r) return; v = l % r; s = sl % sr; break;
	case SPECIAL_LEFTSHIFT: shift = check_shift_count(expr, ctype, r); v = l << shift; s = v; break; 
	case SPECIAL_RIGHTSHIFT:shift = check_shift_count(expr, ctype, r); v = l >> shift; s = sl >> shift; break;
	default: return;
	}
	if (is_signed)
		v = s;
	mask = mask | (mask-1);
	expr->value = v & mask;
	expr->type = EXPR_VALUE;
}

static void simplify_cmp_binop(struct expression *expr, struct symbol *ctype)
{
	struct expression *left = expr->left, *right = expr->right;
	unsigned long long l, r, mask;
	signed long long sl, sr;

	if (left->type != EXPR_VALUE || right->type != EXPR_VALUE)
		return;
	l = left->value; r = right->value;
	mask = 1ULL << (ctype->bit_size-1);
	sl = l; sr = r;
	if (sl & mask)
		sl |= ~(mask-1);
	if (sr & mask)
		sr |= ~(mask-1);
	switch (expr->op) {
	case '<':		expr->value = sl < sr; break;
	case '>':		expr->value = sl > sr; break;
	case SPECIAL_LTE:	expr->value = sl <= sr; break;
	case SPECIAL_GTE:	expr->value = sl >= sr; break;
	case SPECIAL_EQUAL:	expr->value = l == r; break;
	case SPECIAL_NOTEQUAL:	expr->value = l != r; break;
	case SPECIAL_UNSIGNED_LT:expr->value = l < r; break;
	case SPECIAL_UNSIGNED_GT:expr->value = l > r; break;
	case SPECIAL_UNSIGNED_LTE:expr->value = l <= r; break;
	case SPECIAL_UNSIGNED_GTE:expr->value = l >= r; break;
	}
	expr->type = EXPR_VALUE;
}

static void expand_int_binop(struct expression *expr)
{
	simplify_int_binop(expr, expr->ctype);
}

static void expand_logical(struct expression *expr)
{
	struct expression *left = expr->left;
	struct expression *right;

	/* Do immediate short-circuiting ... */
	expand_expression(left);
	if (left->type == EXPR_VALUE) {
		if (expr->op == SPECIAL_LOGICAL_AND) {
			if (!left->value) {
				expr->type = EXPR_VALUE;
				expr->value = 0;
				return;
			}
		} else {
			if (left->value) {
				expr->type = EXPR_VALUE;
				expr->value = 1;
				return;
			}
		}
	}

	right = expr->right;
	expand_expression(right);
	if (left->type == EXPR_VALUE && right->type == EXPR_VALUE) {
		/*
		 * We know the left value doesn't matter, since
		 * otherwise we would have short-circuited it..
		 */
		expr->type = EXPR_VALUE;
		expr->value = right->value;
	}
}

static void expand_binop(struct expression *expr)
{
	expand_int_binop(expr);
}

static void expand_comma(struct expression *expr)
{
	if (expr->left->type == EXPR_VALUE)
		*expr = *expr->right;
}

#define MOD_IGN (MOD_VOLATILE | MOD_CONST)

static int compare_types(int op, struct symbol *left, struct symbol *right)
{
	switch (op) {
	case SPECIAL_EQUAL:
		return !type_difference(left, right, MOD_IGN, MOD_IGN);
	case SPECIAL_NOTEQUAL:
		return type_difference(left, right, MOD_IGN, MOD_IGN) != NULL;
	case '<':
		return left->bit_size < right->bit_size;
	case '>':
		return left->bit_size > right->bit_size;
	case SPECIAL_LTE:
		return left->bit_size <= right->bit_size;
	case SPECIAL_GTE:
		return left->bit_size >= right->bit_size;
	}
	return 0;
}

static void expand_compare(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;

	/* Type comparison? */
	if (left && right && left->type == EXPR_TYPE && right->type == EXPR_TYPE) {
		int op = expr->op;
		expr->type = EXPR_VALUE;
		expr->value = compare_types(op, left->symbol, right->symbol);
		return;
	}
	simplify_cmp_binop(expr, expr->ctype);
}

static void expand_conditional(struct expression *expr)
{
	struct expression *cond = expr->conditional;

	if (cond->type == EXPR_VALUE) {
		struct expression *true, *false;

		true = expr->cond_true ? : cond;
		false = expr->cond_false;

		if (!cond->value)
			true = false;
		*expr = *true;
	}
}
		
static void expand_assignment(struct expression *expr)
{
}

static void expand_addressof(struct expression *expr)
{
}

static void expand_dereference(struct expression *expr)
{
	struct expression *unop = expr->unop;

	/*
	 * NOTE! We get a bogus warning right now for some special
	 * cases: apparently I've screwed up the optimization of
	 * a zero-offset derefence, and the ctype is wrong.
	 *
	 * Leave the warning in anyway, since this is also a good
	 * test for me to get the type evaluation right..
	 */
	if (expr->ctype->ctype.modifiers & MOD_NODEREF)
		warn(unop->pos, "dereference of noderef expression");

	if (unop->type == EXPR_SYMBOL) {
		struct symbol *sym = unop->symbol;

		/* Const symbol with a constant initializer? */
		if (!(sym->ctype.modifiers & (MOD_ASSIGNED | MOD_ADDRESSABLE))) {
			struct expression *value = sym->initializer;
			if (value) {
				if (value->type == EXPR_VALUE) {
					expr->type = EXPR_VALUE;
					expr->value = value->value;
				}
			}
		}
	}
}

static void simplify_preop(struct expression *expr)
{
	struct expression *op = expr->unop;
	unsigned long long v, mask;

	if (op->type != EXPR_VALUE)
		return;
	v = op->value;
	switch (expr->op) {
	case '+': break;
	case '-': v = -v; break;
	case '!': v = !v; break;
	case '~': v = ~v; break;
	default: return;
	}
	mask = 1ULL << (expr->ctype->bit_size-1);
	mask = mask | (mask-1);
	expr->value = v & mask;
	expr->type = EXPR_VALUE;
}

/*
 * Unary post-ops: x++ and x--
 */
static void expand_postop(struct expression *expr)
{
}

static void expand_preop(struct expression *expr)
{
	switch (expr->op) {
	case '*':
		expand_dereference(expr);
		return;

	case '&':
		expand_addressof(expr);
		return;

	case SPECIAL_INCREMENT:
	case SPECIAL_DECREMENT:
		/*
		 * From a type evaluation standpoint the pre-ops are
		 * the same as the postops
		 */
		expand_postop(expr);
		return;

	default:
		break;
	}
	simplify_preop(expr);
}

static void expand_arguments(struct expression_list *head)
{
	struct expression *expr;

	FOR_EACH_PTR (head, expr) {
		expand_expression(expr);
	} END_FOR_EACH_PTR;
}

static void expand_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;

	expand_expression(target);

	/* Simplify normal integer casts.. */
	if (target->type == EXPR_VALUE)
		cast_value(expr, expr->ctype, target, target->ctype);
}

/*
 * expand a call expression with a symbol. This
 * should expand inline functions, and expand
 * builtins.
 */
static void expand_symbol_call(struct expression *expr)
{
	struct expression *fn = expr->fn;
	struct symbol *ctype = fn->ctype;

	if (fn->type != EXPR_PREOP)
		return;

	if (ctype->op && ctype->op->expand) {
		ctype->op->expand(expr);
		return;
	}
}

static void expand_call(struct expression *expr)
{
	struct symbol *sym;
	struct expression *fn = expr->fn;

	sym = fn->ctype;
	expand_arguments(expr->args);
	if (sym->type == SYM_NODE)
		expand_symbol_call(expr);
}

static void expand_expression_list(struct expression_list *list)
{
	struct expression *expr;

	FOR_EACH_PTR(list, expr) {
		expand_expression(expr);
	} END_FOR_EACH_PTR;
}

static void expand_expression(struct expression *expr)
{
	if (!expr)
		return;
	if (!expr->ctype)
		return;

	switch (expr->type) {
	case EXPR_VALUE:
		return;
	case EXPR_STRING:
		return;
	case EXPR_TYPE:
	case EXPR_SYMBOL:
		expand_symbol_expression(expr);
		return;
	case EXPR_BINOP:
		expand_expression(expr->left);
		expand_expression(expr->right);
		expand_binop(expr);
		return;

	case EXPR_LOGICAL:
		expand_logical(expr);
		return;

	case EXPR_COMMA:
		expand_expression(expr->left);
		expand_expression(expr->right);
		expand_comma(expr);
		return;

	case EXPR_COMPARE:
		expand_expression(expr->left);
		expand_expression(expr->right);
		expand_compare(expr);
		return;

	case EXPR_ASSIGNMENT:
		expand_expression(expr->left);
		expand_expression(expr->right);
		expand_assignment(expr);
		return;

	case EXPR_PREOP:
		expand_expression(expr->unop);
		expand_preop(expr);
		return;

	case EXPR_POSTOP:
		expand_expression(expr->unop);
		expand_postop(expr);
		return;

	case EXPR_CAST:
		expand_cast(expr);
		return;

	case EXPR_CALL:
		expand_call(expr);
		return;

	case EXPR_DEREF:
		return;

	case EXPR_BITFIELD:
		expand_expression(expr->address);
		return;

	case EXPR_CONDITIONAL:
		expand_expression(expr->conditional);
		expand_expression(expr->cond_false);
		expand_expression(expr->cond_true);
		expand_conditional(expr);
		return;

	case EXPR_STATEMENT:
		expand_statement(expr->statement);
		return;

	case EXPR_LABEL:
		return;

	case EXPR_INITIALIZER:
		expand_expression_list(expr->expr_list);
		return;

	case EXPR_IDENTIFIER:
		return;

	case EXPR_INDEX:
		return;

	case EXPR_POS:
		expand_expression(expr->init_expr);
		return;

	case EXPR_SIZEOF:
	case EXPR_ALIGNOF:
		warn(expr->pos, "internal front-end error: sizeof in expansion?");
		return;
	}
	return;
}

static void expand_const_expression(struct expression *expr, const char *where)
{
	if (expr) {
		expand_expression(expr);
		if (expr->type != EXPR_VALUE)
			warn(expr->pos, "Expected constant expression in %s", where);
	}
}

void expand_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return;
	base_type = sym->ctype.base_type;
	if (!base_type)
		return;

	expand_expression(sym->initializer);
	/* expand the body of the symbol */
	if (base_type->type == SYM_FN) {
		if (base_type->stmt)
			expand_statement(base_type->stmt);
	}
}

static void expand_return_expression(struct statement *stmt)
{
	expand_expression(stmt->expression);
}

static void expand_if_statement(struct statement *stmt)
{
	struct expression *expr = stmt->if_conditional;

	if (!expr || !expr->ctype)
		return;

	expand_expression(expr);

/* This is only valid if nobody jumps into the "dead" side */
#if 0
	/* Simplify constant conditionals without even evaluating the false side */
	if (expr->type == EXPR_VALUE) {
		struct statement *simple;
		simple = expr->value ? stmt->if_true : stmt->if_false;

		/* Nothing? */
		if (!simple) {
			stmt->type = STMT_NONE;
			return;
		}
		expand_statement(simple);
		*stmt = *simple;
		return;
	}
#endif
	expand_statement(stmt->if_true);
	expand_statement(stmt->if_false);
}

static void expand_statement(struct statement *stmt)
{
	if (!stmt)
		return;

	switch (stmt->type) {
	case STMT_RETURN:
		expand_return_expression(stmt);
		return;

	case STMT_EXPRESSION:
		expand_expression(stmt->expression);
		return;

	case STMT_COMPOUND: {
		struct symbol *sym;
		struct statement *s;

		FOR_EACH_PTR(stmt->syms, sym) {
			expand_symbol(sym);
		} END_FOR_EACH_PTR;
		expand_symbol(stmt->ret);

		FOR_EACH_PTR(stmt->stmts, s) {
			expand_statement(s);
		} END_FOR_EACH_PTR;
		return;
	}

	case STMT_IF:
		expand_if_statement(stmt);
		return;

	case STMT_ITERATOR:
		expand_expression(stmt->iterator_pre_condition);
		expand_expression(stmt->iterator_post_condition);
		expand_statement(stmt->iterator_pre_statement);
		expand_statement(stmt->iterator_statement);
		expand_statement(stmt->iterator_post_statement);
		return;

	case STMT_SWITCH:
		expand_expression(stmt->switch_expression);
		expand_statement(stmt->switch_statement);
		return;

	case STMT_CASE:
		expand_const_expression(stmt->case_expression, "case statement");
		expand_const_expression(stmt->case_to, "case statement");
		expand_statement(stmt->case_statement);
		return;

	case STMT_LABEL:
		expand_statement(stmt->label_statement);
		return;

	case STMT_GOTO:
		expand_expression(stmt->goto_expression);
		return;

	case STMT_NONE:
		break;
	case STMT_ASM:
		/* FIXME! Do the asm parameter evaluation! */
		break;
	}
}

long long get_expression_value(struct expression *expr)
{
	long long value, mask;
	struct symbol *ctype;

	if (!expr)
		return 0;
	ctype = evaluate_expression(expr);
	if (!ctype) {
		warn(expr->pos, "bad constant expression type");
		return 0;
	}
	expand_expression(expr);
	if (expr->type != EXPR_VALUE) {
		warn(expr->pos, "bad constant expression");
		return 0;
	}

	value = expr->value;
	mask = 1ULL << (ctype->bit_size-1);

	if (value & mask) {
		while (ctype->type != SYM_BASETYPE)
			ctype = ctype->ctype.base_type;
		if (!(ctype->ctype.modifiers & MOD_UNSIGNED))
			value = value | mask | ~(mask-1);
	}
	return value;
}
