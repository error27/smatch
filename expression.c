/*
 * sparse/expression.c
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 *
 * This is the expression parsing part of parsing C.
 */
#define _ISOC99_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"
#include "target.h"

static int match_oplist(int op, ...)
{
	va_list args;

	va_start(args, op);
	for (;;) {
		int nextop = va_arg(args, int);
		if (!nextop)
			return 0;
		if (op == nextop)
			return 1;
	}
}

static struct token *comma_expression(struct token *, struct expression **);

struct token *parens_expression(struct token *token, struct expression **expr, const char *where)
{
	token = expect(token, '(', where);
	if (match_op(token, '{')) {
		struct expression *e = alloc_expression(token->pos, EXPR_STATEMENT);
		struct statement *stmt = alloc_statement(token->pos, STMT_COMPOUND);
		*expr = e;
		e->statement = stmt;
		start_symbol_scope();
		token = compound_statement(token->next, stmt);
		end_symbol_scope();
		token = expect(token, '}', "at end of statement expression");
	} else
		token = parse_expression(token, expr);
	return expect(token, ')', where);
}

static struct token *string_expression(struct token *token, struct expression *expr)
{
	struct string *string = token->string;
	struct token *next = token->next;

	if (token_type(next) == TOKEN_STRING) {
		int totlen = string->length-1;
		char *data;

		do {
			totlen += next->string->length-1;
			next = next->next;
		} while (token_type(next) == TOKEN_STRING);

		if (totlen > MAX_STRING) {
			warn(token->pos, "trying to concatenate %d-character string (%d bytes max)", totlen, MAX_STRING);
			totlen = MAX_STRING;
		}

		string = __alloc_string(totlen+1);
		string->length = totlen+1;
		data = string->data;
		next = token;
		do {
			struct string *s = next->string;
			int len = s->length-1;

			if (len > totlen)
				len = totlen;
			totlen -= len;

			next = next->next;
			memcpy(data, s->data, len);
			data += len;
		} while (token_type(next) == TOKEN_STRING);
		*data = '\0';
	}
	expr->string = string;
	return next;
}

#ifndef ULLONG_MAX
#define ULLONG_MAX (~0ULL)
#endif

static void get_number_value(struct expression *expr, struct token *token)
{
	const char *str = token->number;
	unsigned long long value;
	char *end;
	unsigned long modifiers = 0;
	int overflow = 0, do_warn = 0;
	int try_unsigned = 1;
	int bits;

	errno = 0;
	value = strtoull(str, &end, 0);
	if (end == str)
		goto Float;
	if (value == ULLONG_MAX && errno == ERANGE)
		overflow = 1;
	while (1) {
		unsigned long added;
		char c = *end++;
		if (!c) {
			break;
		} else if (c == 'u' || c == 'U') {
			added = MOD_UNSIGNED;
		} else if (c == 'l' || c == 'L') {
			added = MOD_LONG;
			if (*end == c) {
				added |= MOD_LONGLONG;
				end++;
			}
		} else
			goto Float;
		if (modifiers & added)
			goto Enoint;
		modifiers |= added;
	}
	if (overflow)
		goto Eoverflow;
	/* OK, it's a valid integer */
	/* decimals can be unsigned only if directly specified as such */
	if (str[0] != '0' && !(modifiers & MOD_UNSIGNED))
		try_unsigned = 0;
	if (!(modifiers & MOD_LONG)) {
		bits = bits_in_int - 1;
		if (!(value & (~1ULL << bits))) {
			if (!(value & (1ULL << bits))) {
				goto got_it;
			} else if (try_unsigned) {
				modifiers |= MOD_UNSIGNED;
				goto got_it;
			}
		}
		modifiers |= MOD_LONG;
		do_warn = 1;
	}
	if (!(modifiers & MOD_LONGLONG)) {
		bits = bits_in_long - 1;
		if (!(value & (~1ULL << bits))) {
			if (!(value & (1ULL << bits))) {
				goto got_it;
			} else if (try_unsigned) {
				modifiers |= MOD_UNSIGNED;
				goto got_it;
			}
			do_warn |= 2;
		}
		modifiers |= MOD_LONGLONG;
		do_warn |= 1;
	}
	bits = bits_in_longlong - 1;
	if (value & (~1ULL << bits))
		goto Eoverflow;
	if (!(value & (1ULL << bits)))
		goto got_it;
	if (!try_unsigned)
		warn(expr->pos, "decimal constant %s is too big for long long",
			show_token(token));
	modifiers |= MOD_UNSIGNED;
got_it:
	if (do_warn)
		warn(expr->pos, "constant %s is so big it is%s%s%s",
			show_token(token),
			(modifiers & MOD_UNSIGNED) ? " unsigned":"",
			(modifiers & MOD_LONG) ? " long":"",
			(modifiers & MOD_LONGLONG) ? " long":"");
	if (do_warn & 2)
		warn(expr->pos,
			"decimal constant %s is between LONG_MAX and ULONG_MAX."
			" For C99 that means long long, C90 compilers are very "
			"likely to produce unsigned long (and a warning) here",
			show_token(token));
        expr->type = EXPR_VALUE;
        expr->ctype = ctype_integer(modifiers);
        expr->value = value;
	return;
Eoverflow:
	error(expr->pos, "constant %s is too big even for unsigned long long",
			show_token(token));
	return;
Float:
	expr->fvalue = strtold(str, &end);
	if (str == end)
		goto Enoint;

	if (*end && end[1])
		goto Enoint;

	if (*end == 'f' || *end == 'F')
		expr->ctype = &float_ctype;
	else if (*end == 'l' || *end == 'L')
		expr->ctype = &ldouble_ctype;
	else if (!*end)
		expr->ctype = &double_ctype;
	else
		goto Enoint;

	expr->type = EXPR_FVALUE;
	return;

Enoint:
	error(expr->pos, "constant %s is not a valid number", show_token(token));
}

struct token *primary_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	switch (token_type(token)) {
	case TOKEN_CHAR:
		expr = alloc_expression(token->pos, EXPR_VALUE);   
		expr->ctype = &int_ctype; 
		expr->value = (unsigned char) token->character;
		token = token->next;
		break;

	case TOKEN_NUMBER:
		expr = alloc_expression(token->pos, EXPR_VALUE);
		get_number_value(expr, token);
		token = token->next;
		break;

	case TOKEN_IDENT: {
		struct symbol *sym = lookup_symbol(token->ident, NS_SYMBOL | NS_TYPEDEF);
		struct token *next = token->next;

		expr = alloc_expression(token->pos, EXPR_SYMBOL);

		/*
		 * We support types as real first-class citizens, with type
		 * comparisons etc:
		 *
		 *	if (typeof(a) == int) ..
		 */
		if (sym && sym->namespace == NS_TYPEDEF) {
			warn(token->pos, "typename in expression");
			sym = NULL;
		}
		expr->symbol_name = token->ident;
		expr->symbol = sym;
		token = next;
		break;
	}

	case TOKEN_STRING: {
		expr = alloc_expression(token->pos, EXPR_STRING);
		token = string_expression(token, expr);
		break;
	}

	case TOKEN_SPECIAL:
		if (token->special == '(') {
			expr = alloc_expression(token->pos, EXPR_PREOP);
			expr->op = '(';
			token = parens_expression(token, &expr->unop, "in expression");
			break;
		}
		if (token->special == '[' && lookup_type(token->next)) {
			expr = alloc_expression(token->pos, EXPR_TYPE);
			token = typename(token->next, &expr->symbol);
			token = expect(token, ']', "in type expression");
			break;
		}
			
	default:
		;
	}
	*tree = expr;
	return token;
}

static struct token *expression_list(struct token *token, struct expression_list **list)
{
	while (!match_op(token, ')')) {
		struct expression *expr = NULL;
		token = assignment_expression(token, &expr);
		if (!expr)
			break;
		add_expression(list, expr);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	return token;
}

/*
 * extend to deal with the ambiguous C grammar for parsing
 * a cast expressions followed by an initializer.
 */
static struct token *postfix_expression(struct token *token, struct expression **tree, struct expression *cast_init_expr)
{
	struct expression *expr = cast_init_expr;

	if (!expr)
		token = primary_expression(token, &expr);

	while (expr && token_type(token) == TOKEN_SPECIAL) {
		switch (token->special) {
		case '[': {			/* Array dereference */
			struct expression *deref = alloc_expression(token->pos, EXPR_PREOP);
			struct expression *add = alloc_expression(token->pos, EXPR_BINOP);

			deref->op = '*';
			deref->unop = add;

			add->op = '+';
			add->left = expr;
			token = parse_expression(token->next, &add->right);
			token = expect(token, ']', "at end of array dereference");
			expr = deref;
			continue;
		}
		case SPECIAL_INCREMENT:		/* Post-increment */
		case SPECIAL_DECREMENT:	{	/* Post-decrement */
			struct expression *post = alloc_expression(token->pos, EXPR_POSTOP);
			post->op = token->special;
			post->unop = expr;
			expr = post;
			token = token->next;
			continue;
		}
		case SPECIAL_DEREFERENCE: {	/* Structure pointer member dereference */
			/* "x->y" is just shorthand for "(*x).y" */
			struct expression *inner = alloc_expression(token->pos, EXPR_PREOP);
			inner->op = '*';
			inner->unop = expr;
			expr = inner;
		}
		/* Fallthrough!! */
		case '.': {			/* Structure member dereference */
			struct expression *deref = alloc_expression(token->pos, EXPR_DEREF);
			deref->op = '.';
			deref->deref = expr;
			token = token->next;
			if (token_type(token) != TOKEN_IDENT) {
				warn(token->pos, "Expected member name");
				break;
			}
			deref->member = token->ident;
			token = token->next;
			expr = deref;
			continue;
		}

		case '(': {			/* Function call */
			struct expression *call = alloc_expression(token->pos, EXPR_CALL);
			call->op = '(';
			call->fn = expr;
			token = expression_list(token->next, &call->args);
			token = expect(token, ')', "in function call");
			expr = call;
			continue;
		}

		default:
			break;
		}
		break;
	}
	*tree = expr;
	return token;
}

static struct token *cast_expression(struct token *token, struct expression **tree);
static struct token *unary_expression(struct token *token, struct expression **tree)
{
	if (token_type(token) == TOKEN_IDENT) {
		if (token->ident == &sizeof_ident) {
			struct expression *sizeof_ex 
				= alloc_expression(token->pos, EXPR_SIZEOF);
			*tree = sizeof_ex;
			tree = &sizeof_ex->unop;
			token = token->next;
			if (!match_op(token, '(') || !lookup_type(token->next))
				return unary_expression(token, &sizeof_ex->cast_expression);
			token = typename(token->next, &sizeof_ex->cast_type);
			return expect(token, ')', "at end of sizeof type-name");
		} else if (token->ident == &__alignof___ident) {
			struct expression *alignof_ex 
				= alloc_expression(token->pos, EXPR_ALIGNOF);
			*tree = alignof_ex;
			tree = &alignof_ex->unop;
			token = token->next;
			if (!match_op(token, '(') || !lookup_type(token->next))
				return unary_expression(token, &alignof_ex->cast_expression);
			token = typename(token->next, &alignof_ex->cast_type);
			return expect(token, ')', "at end of alignof type-name");
		}
	}

	if (token_type(token) == TOKEN_SPECIAL) {
		if (match_oplist(token->special,
		    SPECIAL_INCREMENT, SPECIAL_DECREMENT,
		    '&', '*', '+', '-', '~', '!', 0)) {
		    	struct expression *unop;
			struct expression *unary;
			struct token *next;

			next = cast_expression(token->next, &unop);
			if (!unop) {
				warn(token->pos, "Syntax error in unary expression");
				return next;
			}
			unary = alloc_expression(token->pos, EXPR_PREOP);
			unary->op = token->special;
			unary->unop = unop;
			*tree = unary;
			return next;
		}

		/* Gcc extension: &&label gives the address of a label */
		if (match_op(token, SPECIAL_LOGICAL_AND) &&
		    token_type(token->next) == TOKEN_IDENT) {
			struct expression *label = alloc_expression(token->pos, EXPR_LABEL);
			label->label_symbol = label_symbol(token->next);
			*tree = label;
			return token->next->next;
		}
						
	}
			
	return postfix_expression(token, tree, NULL);
}

/*
 * Ambiguity: a '(' can be either a cast-expression or
 * a primary-expression depending on whether it is followed
 * by a type or not. 
 *
 * additional ambiguity: a "cast expression" followed by
 * an initializer is really a postfix-expression.
 */
static struct token *cast_expression(struct token *token, struct expression **tree)
{
	if (match_op(token, '(')) {
		struct token *next = token->next;
		if (lookup_type(next)) {
			struct expression *cast = alloc_expression(next->pos, EXPR_CAST);
			struct symbol *sym;

			token = typename(next, &sym);
			cast->cast_type = sym;
			token = expect(token, ')', "at end of cast operator");
			if (match_op(token, '{')) {
				token = initializer(&cast->cast_expression, token);
				return postfix_expression(token, tree, cast);
			}
			*tree = cast;
			token = cast_expression(token, &cast->cast_expression);
			return token;
		}
	}
	return unary_expression(token, tree);
}

/*
 * Generic left-to-right binop parsing
 *
 * This _really_ needs to be inlined, because that makes the inner
 * function call statically deterministic rather than a totally
 * unpredictable indirect call. But gcc-3 is so "clever" that it
 * doesn't do so by default even when you tell it to inline it.
 *
 * Making it a macro avoids the inlining problem, and also means
 * that we can pass in the op-comparison as an expression rather
 * than create a data structure for it.
 */

#define LR_BINOP_EXPRESSION(token, tree, type, inner, compare)		\
	struct expression *left = NULL;					\
	struct token * next = inner(token, &left);			\
									\
	if (left) {							\
		while (token_type(next) == TOKEN_SPECIAL) {		\
			struct expression *top, *right = NULL;		\
			int op = next->special;				\
									\
			if (!(compare))					\
				goto out;				\
			top = alloc_expression(next->pos, type);	\
			next = inner(next->next, &right);		\
			if (!right) {					\
				warn(next->pos, "No right hand side of '%s'-expression", show_special(op));	\
				break;					\
			}						\
			top->op = op;					\
			top->left = left;				\
			top->right = right;				\
			left = top;					\
		}							\
	}								\
out:									\
	*tree = left;							\
	return next;							\


static struct token *multiplicative_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, cast_expression,
		(op == '*') || (op == '/') || (op == '%')
	);
}

static struct token *additive_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, multiplicative_expression,
		(op == '+') || (op == '-')
	);
}

static struct token *shift_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, additive_expression,
		(op == SPECIAL_LEFTSHIFT) || (op == SPECIAL_RIGHTSHIFT)
	);
}

static struct token *relational_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_COMPARE, shift_expression,
		(op == '<') || (op == '>') ||
		(op == SPECIAL_LTE) || (op == SPECIAL_GTE)
	);
}

static struct token *equality_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_COMPARE, relational_expression,
		(op == SPECIAL_EQUAL) || (op == SPECIAL_NOTEQUAL)
	);
}

static struct token *bitwise_and_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, equality_expression,
		(op == '&')
	);
}

static struct token *bitwise_xor_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, bitwise_and_expression,
		(op == '^')
	);
}

static struct token *bitwise_or_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_BINOP, bitwise_xor_expression,
		(op == '|')
	);
}

static struct token *logical_and_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_LOGICAL, bitwise_or_expression,
		(op == SPECIAL_LOGICAL_AND)
	);
}

static struct token *logical_or_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_LOGICAL, logical_and_expression,
		(op == SPECIAL_LOGICAL_OR)
	);
}

struct token *conditional_expression(struct token *token, struct expression **tree)
{
	token = logical_or_expression(token, tree);
	if (match_op(token, '?')) {
		struct expression *expr = alloc_expression(token->pos, EXPR_CONDITIONAL);
		expr->op = token->special;
		expr->left = *tree;
		*tree = expr;
		token = parse_expression(token->next, &expr->cond_true);
		token = expect(token, ':', "in conditional expression");
		token = conditional_expression(token, &expr->cond_false);
	}
	return token;
}

struct token *assignment_expression(struct token *token, struct expression **tree)
{
	token = conditional_expression(token, tree);
	if (token_type(token) == TOKEN_SPECIAL) {
		static const int assignments[] = {
			'=',
			SPECIAL_ADD_ASSIGN, SPECIAL_SUB_ASSIGN,
			SPECIAL_MUL_ASSIGN, SPECIAL_DIV_ASSIGN,
			SPECIAL_MOD_ASSIGN, SPECIAL_SHL_ASSIGN,
			SPECIAL_SHR_ASSIGN, SPECIAL_AND_ASSIGN,
			SPECIAL_OR_ASSIGN,  SPECIAL_XOR_ASSIGN };
		int i, op = token->special;
		for (i = 0; i < sizeof(assignments)/sizeof(int); i++)
			if (assignments[i] == op) {
				struct expression * expr = alloc_expression(token->pos, EXPR_ASSIGNMENT);
				expr->left = *tree;
				expr->op = op;
				*tree = expr;
				return assignment_expression(token->next, &expr->right);
			}
	}
	return token;
}

static struct token *comma_expression(struct token *token, struct expression **tree)
{
	LR_BINOP_EXPRESSION(
		token, tree, EXPR_COMMA, assignment_expression,
		(op == ',')
	);
}

struct token *parse_expression(struct token *token, struct expression **tree)
{
	return comma_expression(token,tree);
}


