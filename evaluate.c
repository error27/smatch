/*
 * sparse/evaluate.c
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 *
 * Evaluate constant expressions.
 */
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
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

static int evaluate_symbol(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		warn(expr->token, "undefined identifier '%s'", show_token(expr->token));
		return 0;
	}
	examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return 0;

	/* The ctype of a symbol expression is the symbol itself! */
	expr->ctype = sym;

	/* enum's can be turned into plain values */
	if (base_type->type == SYM_ENUM) {
		expr->type = EXPR_VALUE;
		expr->value = sym->value;
	}
	return 1;
}

static int get_int_value(struct expression *expr, const char *str)
{
	unsigned long long value = 0;
	unsigned int base = 10, digit;

	switch (str[0]) {
	case 'x':
		base = 18;	// the -= 2 for the octal case will
		str++;		// skip the 'x'
	/* fallthrough */
	case 'o':
		str++;		// skip the 'o' or 'x/X'
		base -= 2;	// the fall-through will make this 8
	}
	while ((digit = hexval(*str)) < base) {
		value = value * base + digit;
		str++;
	}
	expr->type = EXPR_VALUE;
	expr->ctype = &int_ctype;
	expr->value = value;
	return 1;
}

static int evaluate_constant(struct expression *expr)
{
	struct token *token = expr->token;

	switch (token->type) {
	case TOKEN_INTEGER:
		return get_int_value(expr, token->integer);

	case TOKEN_CHAR:
		expr->type = EXPR_VALUE;
		expr->ctype = &int_ctype;
		expr->value = (char) token->character;
		return 1;
	case TOKEN_STRING:
		expr->ctype = &string_ctype;
		return 1;
	default:
		warn(token, "non-typed expression");
	}
	return 0;
}

static struct symbol *integer_type(unsigned long mod, int bit_size, int alignment)
{
	/* FIXME! We shouldn't allocate a new one, we should look up a static one! */
	struct symbol *sym = alloc_symbol(NULL, SYM_TYPE);
	mod &= (MOD_CHAR | MOD_SHORT | MOD_LONG | MOD_LONGLONG | MOD_UNSIGNED);
	sym->ctype.base_type = &int_type;
	sym->ctype.modifiers = mod;
	sym->bit_size = bit_size;
	sym->alignment = alignment;
	return sym;
}

static struct symbol *bigger_int_type(struct symbol *left, struct symbol *right)
{
	unsigned long lmod, rmod, mod;

	if (left == right)
		return left;

	if (left->bit_size > right->bit_size)
		return left;

	if (right->bit_size > left->bit_size)
		return right;

	/* Same size integers - promote to unsigned, promote to long */
	lmod = left->ctype.modifiers;
	rmod = right->ctype.modifiers;
	mod = lmod | rmod;
	if (mod == lmod)
		return left;
	if (mod == rmod)
		return right;
	return integer_type(mod, left->bit_size, left->alignment);
}

static struct expression * promote(struct expression *old, struct symbol *type)
{
	struct expression *expr = alloc_expression(old->token, EXPR_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	return expr;
}

static int evaluate_binop(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	/* Integer promotion? */
	if (ltype->ctype.base_type == &int_type && rtype->ctype.base_type == &int_type) {
		struct symbol *ctype = bigger_int_type(ltype, rtype);

		/* Don't bother promoting same-size entities, it only adds clutter */
		if (ltype->bit_size != ctype->bit_size)
			expr->left = promote(left, ctype);
		if (rtype->bit_size != ctype->bit_size)
			expr->right = promote(right, ctype);
		expr->ctype = ctype;
		return 1;
	}
	warn(expr->token, "unexpected types for operation");
	return 0;
}

static int evaluate_preop(struct expression *expr)
{
	return 0;
}

static int evaluate_postop(struct expression *expr)
{
	return 0;
}

int evaluate_expression(struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->ctype)
		return 1;

	switch (expr->type) {
	case EXPR_CONSTANT:
		return evaluate_constant(expr);
	case EXPR_SYMBOL:
		return evaluate_symbol(expr);
	case EXPR_BINOP:
		if (!evaluate_expression(expr->left))
			return 0;
		if (!evaluate_expression(expr->right))
			return 0;
		return evaluate_binop(expr);
	case EXPR_PREOP:
		if (!evaluate_expression(expr->unop))
			return 0;
		return evaluate_preop(expr);
	case EXPR_POSTOP:
		if (!evaluate_expression(expr->unop))
			return 1;
		return evaluate_postop(expr);
	default:
		break;
	}
	return 0;
}

long long get_expression_value(struct expression *expr)
{
	long long left, middle, right;

	switch (expr->type) {
	case EXPR_SIZEOF:
		if (expr->cast_type) {
			examine_symbol_type(expr->cast_type);
			if (expr->cast_type->bit_size & 7) {
				warn(expr->token, "type has no size");
				return 0;
			}
			return expr->cast_type->bit_size >> 3;
		}
		warn(expr->token, "expression sizes not yet supported");
		return 0;
	case EXPR_CONSTANT:
		evaluate_constant(expr);
		if (expr->type == EXPR_VALUE)
			return expr->value;
		return 0;
	case EXPR_SYMBOL: {
		struct symbol *sym = expr->symbol;
		if (!sym || !sym->ctype.base_type || sym->ctype.base_type->type != SYM_ENUM) {
			warn(expr->token, "undefined identifier in constant expression");
			return 0;
		}
		return sym->value;
	}

#define OP(x,y)	case x: return left y right;
	case EXPR_BINOP:
		left = get_expression_value(expr->left);
		if (!left && expr->op == SPECIAL_LOGICAL_AND)
			return 0;
		if (left && expr->op == SPECIAL_LOGICAL_OR)
			return 1;
		right = get_expression_value(expr->right);
		switch (expr->op) {
			OP('+',+); OP('-',-); OP('*',*); OP('/',/);
			OP('%',%); OP('<',<); OP('>',>);
			OP('&',&);OP('|',|);OP('^',^);
			OP(SPECIAL_EQUAL,==); OP(SPECIAL_NOTEQUAL,!=);
			OP(SPECIAL_LTE,<=); OP(SPECIAL_LEFTSHIFT,<<);
			OP(SPECIAL_RIGHTSHIFT,>>); OP(SPECIAL_GTE,>=);
			OP(SPECIAL_LOGICAL_AND,&&);OP(SPECIAL_LOGICAL_OR,||);
		}
		break;

#undef OP
#define OP(x,y)	case x: return y left;
	case EXPR_PREOP:
		left = get_expression_value(expr->unop);
		switch (expr->op) {
			OP('+', +); OP('-', -); OP('!', !); OP('~', ~); OP('(', );
		}
		break;

	case EXPR_CONDITIONAL:
		left = get_expression_value(expr->conditional);
		if (!expr->cond_true)
			middle = left;
		else
			middle = get_expression_value(expr->cond_true);
		right = get_expression_value(expr->cond_false);
		return left ? middle : right;
	}
	error(expr->token, "bad constant expression");
	return 0;
}
