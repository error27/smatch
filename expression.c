/*
 * sparse/expression.c
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
 *
 * This is the expression parsing part of parsing C.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

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
		int totlen = string->length;
		char *data;

		do {
			totlen += next->string->length-1;
			next = next->next;
		} while (token_type(next) == TOKEN_STRING);

		string = __alloc_string(totlen);
		string->length = totlen;
		data = string->data;
		next = token;
		do {
			struct string *s = next->string;
			int len = s->length;

			next = next->next;
			memcpy(data, s->data, len);
			data += len-1;
		} while (token_type(next) == TOKEN_STRING);
	}
	expr->string = string;
	return next;
}

static void get_int_value(struct expression *expr, const char *str)
{
	unsigned long long value = 0;
	unsigned int base = 10, digit, bits;
	unsigned long modifiers, extramod;

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
	modifiers = 0;
	for (;;) {
		char c = *str++;
		if (c == 'u' || c == 'U') {
			modifiers |= MOD_UNSIGNED;
			continue;
		}
		if (c == 'l' || c == 'L') {
			if (modifiers & MOD_LONG)
				modifiers |= MOD_LONGLONG;
			modifiers |= MOD_LONG;
			continue;
		}
		break;
	}

	bits = BITS_IN_LONGLONG;
	extramod = 0;
	if (!(modifiers & MOD_LONGLONG)) {
		if (value & (~1ULL << (BITS_IN_LONG-1))) {
			extramod = MOD_LONGLONG | MOD_LONG;
		} else {
			bits = BITS_IN_LONG;
			if (!(modifiers & MOD_LONG)) {
				if (value & (~1ULL << (BITS_IN_INT-1))) {
					extramod = MOD_LONG;
				} else
					bits = BITS_IN_INT;
			}
		}
	}
	if (!(modifiers & MOD_UNSIGNED)) {
		if (value & (1ULL << (bits-1))) {
			extramod |= MOD_UNSIGNED;
		}
	}
	if (extramod) {
		/*
		 * Special case: "int" gets promoted directly to "long"
		 * for normal decimal numbers..
		 */
		modifiers |= extramod;
		if (base == 10 && modifiers == MOD_UNSIGNED) {
			modifiers = MOD_LONG;
			if (BITS_IN_LONG == BITS_IN_INT)
				modifiers = MOD_LONG | MOD_UNSIGNED;
		}
		warn(expr->pos, "value is so big it is%s%s%s",
			(modifiers & MOD_UNSIGNED) ? " unsigned":"",
			(modifiers & MOD_LONG) ? " long":"",
			(modifiers & MOD_LONGLONG) ? " long":"");
	}

	expr->type = EXPR_VALUE;
	expr->ctype = ctype_integer(modifiers);
	expr->value = value;
}

struct token *primary_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	switch (token_type(token)) {
	case TOKEN_FP:
		expr = alloc_expression(token->pos, EXPR_VALUE);
		expr->ctype = &double_ctype;
		expr->value = 0;
		warn(token->pos, "FP values not yet implemented");
		token = token->next;
		break;

	case TOKEN_CHAR:
		expr = alloc_expression(token->pos, EXPR_VALUE);   
		expr->ctype = &int_ctype; 
		expr->value = (unsigned char) token->character;
		token = token->next;
		break;

	case TOKEN_INTEGER:
		expr = alloc_expression(token->pos, EXPR_VALUE);
		get_int_value(expr, token->integer);
		token = token->next;
		break;

	case TOKEN_IDENT: {
		expr = alloc_expression(token->pos, EXPR_SYMBOL);
		expr->symbol_name = token->ident;
		expr->symbol = lookup_symbol(token->ident, NS_SYMBOL);
		token = token->next;
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

static struct token *postfix_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

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
		case '.':			/* Structure member dereference */
		case SPECIAL_DEREFERENCE: {	/* Structure pointer member dereference */
			struct expression *deref = alloc_expression(token->pos, EXPR_DEREF);
			deref->op = token->special;
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
	if (token_type(token) == TOKEN_IDENT &&
	    (token->ident == &sizeof_ident ||
	     token->ident == &__alignof___ident)) {
		struct expression *sizeof_ex = alloc_expression(token->pos, EXPR_SIZEOF);
		*tree = sizeof_ex;
		tree = &sizeof_ex->unop;
		token = token->next;
		if (!match_op(token, '(') || !lookup_type(token->next))
			return unary_expression(token, &sizeof_ex->cast_expression);
		token = typename(token->next, &sizeof_ex->cast_type);
		return expect(token, ')', "at end of sizeof type-name");
	}

	if (token_type(token) == TOKEN_SPECIAL) {
		if (match_oplist(token->special,
		    SPECIAL_INCREMENT, SPECIAL_DECREMENT,
		    '&', '*', '+', '-', '~', '!', 0)) {
			struct expression *unary = alloc_expression(token->pos, EXPR_PREOP);
			unary->op = token->special;
			*tree = unary;
			return cast_expression(token->next, &unary->unop);
		}
	}
			
	return postfix_expression(token, tree);
}

/*
 * Ambiguity: a '(' can be either a cast-expression or
 * a primary-expression depending on whether it is followed
 * by a type or not. 
 */
static struct token *cast_expression(struct token *token, struct expression **tree)
{
	if (match_op(token, '(')) {
		struct token *next = token->next;
		if (lookup_type(next)) {
			struct expression *cast = alloc_expression(next->pos, EXPR_CAST);
			struct symbol *sym;

			token = typename(next, &sym);
			cast->cast_type = sym->ctype.base_type;
			token = expect(token, ')', "at end of cast operator");
			*tree = cast;
			if (match_op(token, '{'))
				return initializer(&cast->cast_expression, token);
			token = cast_expression(token, &cast->cast_expression);
			return token;
		}
	}
	return unary_expression(token, tree);
}

/* Generic left-to-right binop parsing */
static struct token *lr_binop_expression(struct token *token, struct expression **tree,
	enum expression_type type, struct token *(*inner)(struct token *, struct expression **), ...)
{
	struct expression *left = NULL;
	struct token * next = inner(token, &left);

	if (left) {
		while (token_type(next) == TOKEN_SPECIAL) {
			struct expression *top, *right = NULL;
			int op = next->special;
			va_list args;

			va_start(args, inner);
			for (;;) {
				int nextop = va_arg(args, int);
				if (!nextop)
					goto out;
				if (op == nextop)
					break;
			}
			va_end(args);
			top = alloc_expression(next->pos, type);
			next = inner(next->next, &right);
			if (!right) {
				warn(next->pos, "No right hand side of '%s'-expression", show_special(op));
				break;
			}
			top->op = op;
			top->left = left;
			top->right = right;
			left = top;
		}
	}
out:
	*tree = left;
	return next;
}

static struct token *multiplicative_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, cast_expression, '*', '/', '%', 0);
}

static struct token *additive_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, multiplicative_expression, '+', '-', 0);
}

static struct token *shift_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, additive_expression, SPECIAL_LEFTSHIFT, SPECIAL_RIGHTSHIFT, 0);
}

static struct token *relational_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_COMPARE, shift_expression, '<', '>', SPECIAL_LTE, SPECIAL_GTE, 0);
}

static struct token *equality_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_COMPARE, relational_expression, SPECIAL_EQUAL, SPECIAL_NOTEQUAL, 0);
}

static struct token *bitwise_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, equality_expression, '&', 0);
}

static struct token *bitwise_xor_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, bitwise_and_expression, '^', 0);
}

static struct token *bitwise_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_BINOP, bitwise_xor_expression, '|', 0);
}

static struct token *logical_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_LOGICAL, bitwise_or_expression, SPECIAL_LOGICAL_AND, 0);
}

static struct token *logical_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, EXPR_LOGICAL, logical_and_expression, SPECIAL_LOGICAL_OR, 0);
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
	return lr_binop_expression(token, tree, EXPR_COMMA, assignment_expression, ',', 0);
}

struct token *parse_expression(struct token *token, struct expression **tree)
{
	return comma_expression(token,tree);
}


