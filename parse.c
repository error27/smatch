/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "token.h"
#include "parse.h"

void show_expression(struct expression *expr)
{
	if (!expr)
		return;

	switch (expr->type) {
	case EXPR_IDENT:
		printf("%s", show_token(expr->token));
		break;
	case EXPR_BINOP:
		printf("<");
		show_expression(expr->left);
		printf(" %s ", show_special(expr->op));
		show_expression(expr->right);
		printf(">");
		break;
	case EXPR_UNARY:
		printf("(");
		show_expression(expr->unop);
		printf(")");
		break;
	default:
		printf("WTF");
	}
}

static struct expression *alloc_expression(struct token *token, int type)
{
	struct expression *expr = malloc(sizeof(struct expression));

	if (!expr)
		die("Unable to allocate expression");
	memset(expr, 0, sizeof(*expr));
	expr->type = type;
	expr->token = token;
	return expr;
}

struct token *cast_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	if (token) {
		switch (token->value.type) {
		case TOKEN_IDENT:
			expr = alloc_expression(token, EXPR_IDENT);
			token = token->next;
			break;
		case TOKEN_SPECIAL:
			if (token->value.special == '(') {
				expr = alloc_expression(token, EXPR_UNARY);
				expr->op = '(';
				token = parse_expression(token->next, &expr->unop);
				if (!token || token->value.type != TOKEN_SPECIAL || token->value.special != ')')
					warn(token, "Expected ')'");
				else
					token = token->next;
				break;
			}
		default:
			warn(token, "Syntax error");
		}
	}
	*tree = expr;
	return token;
}

/* Generic left-to-right binop parsing */
struct token *lr_binop_expression(struct token *token, struct expression **tree,
	struct token *(*inner)(struct token *, struct expression **), ...)
{
	struct expression *left = NULL;
	struct token * next = inner(token, &left);

	if (left) {
		while (next && next->value.type == TOKEN_SPECIAL) {
			struct expression *top, *right = NULL;
			int op = next->value.special;
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
				warn(token, "Syntax error");
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

struct token *multiplicative_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, cast_expression, '*', '/', '%', 0);
}

struct token *additive_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, multiplicative_expression, '+', '-', 0);
}

struct token *shift_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, additive_expression, SPECIAL_LEFTSHIFT, SPECIAL_RIGHTSHIFT, 0);
}

struct token *relational_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, shift_expression, '<', '>', SPECIAL_LTE, SPECIAL_GTE, 0);
}

struct token *equality_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, relational_expression, SPECIAL_EQUAL, SPECIAL_NOTEQUAL, 0);
}

struct token *bitwise_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, equality_expression, '&', 0);
}

struct token *bitwise_xor_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_and_expression, '^', 0);
}

struct token *bitwise_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_xor_expression, '|', 0);
}

struct token *logical_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_or_expression, SPECIAL_LOGICAL_AND, 0);
}

struct token *logical_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, logical_and_expression, SPECIAL_LOGICAL_OR, 0);
}

struct token *parse_expression(struct token *token, struct expression **tree)
{
	return logical_or_expression(token,tree);
}
