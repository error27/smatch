/*
 * sparse/expression.c
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
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
		struct expression *e = alloc_expression(token, EXPR_STATEMENT);
		struct statement *stmt = alloc_statement(token, STMT_COMPOUND);
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

struct token *primary_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	switch (token->type) {
	case TOKEN_INTEGER:
	case TOKEN_FP:
	case TOKEN_CHAR:
		expr = alloc_expression(token, EXPR_CONSTANT);
		token = token->next;
		break;

	case TOKEN_IDENT: {
		expr = alloc_expression(token, EXPR_SYMBOL);
		expr->symbol = lookup_symbol(token->ident, NS_SYMBOL);
		token = token->next;
		break;
	}

	case TOKEN_STRING:
		expr = alloc_expression(token, EXPR_CONSTANT);
		do {
			token = token->next;
		} while (token->type == TOKEN_STRING);
		break;

	case TOKEN_SPECIAL:
		if (token->special == '(') {
			expr = alloc_expression(token, EXPR_PREOP);
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

static struct token *postfix_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	token = primary_expression(token, &expr);
	while (expr && token->type == TOKEN_SPECIAL) {
		switch (token->special) {
		case '[': {			/* Array dereference */
			struct expression *deref = alloc_expression(token, EXPR_PREOP);
			struct expression *add = alloc_expression(token, EXPR_BINOP);

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
			struct expression *post = alloc_expression(token, EXPR_POSTOP);
			post->op = token->special;
			post->unop = expr;
			expr = post;
			token = token->next;
			continue;
		}
		case '.':			/* Structure member dereference */
		case SPECIAL_DEREFERENCE: {	/* Structure pointer member dereference */
			struct expression *deref = alloc_expression(token, EXPR_DEREF);
			deref->op = token->special;
			deref->deref = expr;
			token = token->next;
			if (token->type != TOKEN_IDENT) {
				warn(token, "Expected member name");
				break;
			}
			deref->member = token;
			token = token->next;
			expr = deref;
			continue;
		}

		case '(': {			/* Function call */
			struct expression *call = alloc_expression(token, EXPR_CALL);
			call->op = '(';
			call->left = expr;
			token = comma_expression(token->next, &call->right);
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
	if (token->type == TOKEN_IDENT &&
	    (token->ident == &sizeof_ident ||
	     token->ident == &__alignof___ident)) {
		struct expression *sizeof_ex = alloc_expression(token, EXPR_SIZEOF);
		*tree = sizeof_ex;
		tree = &sizeof_ex->unop;
		token = token->next;
		if (!match_op(token, '(') || !lookup_type(token->next))
			return unary_expression(token, &sizeof_ex->cast_expression);
		token = typename(token->next, &sizeof_ex->cast_type);
		return expect(token, ')', "at end of sizeof type-name");
	}

	if (token->type == TOKEN_SPECIAL) {
		if (match_oplist(token->special,
		    SPECIAL_INCREMENT, SPECIAL_DECREMENT,
		    '&', '*', '+', '-', '~', '!', 0)) {
			struct expression *unary = alloc_expression(token, EXPR_PREOP);
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
			struct expression *cast = alloc_expression(next, EXPR_CAST);
			struct symbol *sym;

			token = typename(next, &sym);
			cast->cast_type = sym->ctype.base_type;
			token = expect(token, ')', "at end of cast operator");
			if (match_op(token, '{'))
				return initializer(token, &cast->cast_type->ctype);
			token = cast_expression(token, &cast->cast_expression);
			*tree = cast;
			return token;
		}
	}
	return unary_expression(token, tree);
}

/* Generic left-to-right binop parsing */
static struct token *lr_binop_expression(struct token *token, struct expression **tree,
	struct token *(*inner)(struct token *, struct expression **), ...)
{
	struct expression *left = NULL;
	struct token * next = inner(token, &left);

	if (left) {
		while (next->type == TOKEN_SPECIAL) {
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
			top = alloc_expression(next, EXPR_BINOP);
			next = inner(next->next, &right);
			if (!right) {
				warn(next, "No right hand side of '%s'-expression", show_special(op));
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
	return lr_binop_expression(token, tree, cast_expression, '*', '/', '%', 0);
}

static struct token *additive_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, multiplicative_expression, '+', '-', 0);
}

static struct token *shift_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, additive_expression, SPECIAL_LEFTSHIFT, SPECIAL_RIGHTSHIFT, 0);
}

static struct token *relational_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, shift_expression, '<', '>', SPECIAL_LTE, SPECIAL_GTE, 0);
}

static struct token *equality_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, relational_expression, SPECIAL_EQUAL, SPECIAL_NOTEQUAL, 0);
}

static struct token *bitwise_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, equality_expression, '&', 0);
}

static struct token *bitwise_xor_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_and_expression, '^', 0);
}

static struct token *bitwise_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_xor_expression, '|', 0);
}

static struct token *logical_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_or_expression, SPECIAL_LOGICAL_AND, 0);
}

static struct token *logical_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, logical_and_expression, SPECIAL_LOGICAL_OR, 0);
}

struct token *conditional_expression(struct token *token, struct expression **tree)
{
	token = logical_or_expression(token, tree);
	if (match_op(token, '?')) {
		struct expression *expr = alloc_expression(token, EXPR_CONDITIONAL);
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
	if (token->type == TOKEN_SPECIAL) {
		static const int assignments[] = {
			'=', SPECIAL_ADD_ASSIGN, SPECIAL_MINUS_ASSIGN,
			SPECIAL_TIMES_ASSIGN, SPECIAL_DIV_ASSIGN,
			SPECIAL_MOD_ASSIGN, SPECIAL_SHL_ASSIGN,
			SPECIAL_SHR_ASSIGN, SPECIAL_AND_ASSIGN,
			SPECIAL_OR_ASSIGN, SPECIAL_XOR_ASSIGN };
		int i, op = token->special;
		for (i = 0; i < sizeof(assignments)/sizeof(int); i++)
			if (assignments[i] == op) {
				struct expression * expr = alloc_expression(token, EXPR_BINOP);
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
	return lr_binop_expression(token, tree, assignment_expression, ',', 0);
}

struct token *parse_expression(struct token *token, struct expression **tree)
{
	return comma_expression(token,tree);
}


