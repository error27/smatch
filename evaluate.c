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

struct symbol char_ctype, int_ctype;

static void evaluate_symbol(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return;

	/* The ctype of a symbol expression is the symbol itself! */
	expr->ctype = sym;

	/* enum's can be turned into plain values */
	if (base_type->type == SYM_ENUM) {
		expr->type = EXPR_VALUE;
		expr->value = sym->value;
		return;
	}
}

static unsigned long long get_int_value(const char *str)
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
	return value;
}

static void evaluate_constant(struct expression *expr)
{
	struct token *token = expr->token;
	long long value;

	switch (token->type) {
	case TOKEN_INTEGER:
		value = get_int_value(token->integer);
		expr->type = EXPR_VALUE;
		expr->ctype = &int_ctype;
		expr->value = value;
		return;
	case TOKEN_CHAR:
		value = (char) token->character;
		expr->type = EXPR_VALUE;
		expr->ctype = &int_ctype;
		expr->value = value;
		return;	
	}
}	

void evaluate_expression(struct expression *expr)
{
	if (!expr || expr->ctype)
		return;
	switch (expr->type) {
	case EXPR_CONSTANT:
		evaluate_constant(expr);
		return;
	case EXPR_SYMBOL:
		evaluate_symbol(expr);
		return;
	case EXPR_BINOP:
		evaluate_expression(expr->left);
		evaluate_expression(expr->right);
		return;
	case EXPR_PREOP:
		evaluate_expression(expr->unop);
		return;
	case EXPR_POSTOP:
		evaluate_expression(expr->unop);
		return;
	default:
		break;
	}
}

static long long primary_value(struct token *token)
{
	switch (token->type) {
	case TOKEN_INTEGER:
		return get_int_value(token->integer);
	}
	error(token, "bad constant expression");
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
		return primary_value(expr->token);
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
