#ifndef EXPRESSION_H
#define EXPRESSION_H
/*
 * sparse/expression.h
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved
 *
 * Declarations and helper functions for expression parsing.
 */

/* Constant expression values */
long long get_expression_value(struct expression *);

/* Expression parsing */
struct token *parse_expression(struct token *token, struct expression **tree);
struct token *conditional_expression(struct token *token, struct expression **tree);
struct token *primary_expression(struct token *token, struct expression **tree);
struct token *parens_expression(struct token *token, struct expression **expr, const char *where);
struct token *assignment_expression(struct token *token, struct expression **tree);

static inline struct expression *alloc_expression(struct token *token, int type)
{
	struct expression *expr = __alloc_expression(0);
	expr->type = type;
	expr->token = token;
	return expr;
}

/* Type name parsing */
struct token *typename(struct token *, struct symbol **);

static inline int lookup_type(struct token *token)
{
	if (token->type == TOKEN_IDENT)
		return lookup_symbol(token->ident, NS_TYPEDEF) != NULL;
	return 0;
}

/* Statement parsing */
struct statement *alloc_statement(struct token * token, int type);
struct token *initializer(struct token *token, struct ctype *type);
struct token *compound_statement(struct token *, struct statement *);

/* The preprocessor calls this 'constant_expression()' */
#define constant_expression(token,tree) conditional_expression(token, tree)

#endif
