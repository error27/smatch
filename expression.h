#ifndef EXPRESSION_H
#define EXPRESSION_H
/*
 * sparse/expression.h
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 *
 * Declarations and helper functions for expression parsing.
 */

struct expression_list;

enum expression_type {
	EXPR_VALUE,
	EXPR_STRING,
	EXPR_SYMBOL,
	EXPR_TYPE,
	EXPR_BINOP,
	EXPR_ASSIGNMENT,
	EXPR_LOGICAL,
	EXPR_DEREF,
	EXPR_PREOP,
	EXPR_POSTOP,
	EXPR_CAST,
	EXPR_SIZEOF,
	EXPR_ALIGNOF,
	EXPR_CONDITIONAL,
	EXPR_SELECT,		// a "safe" conditional expression
	EXPR_STATEMENT,
	EXPR_CALL,
	EXPR_COMMA,
	EXPR_COMPARE,
	EXPR_BITFIELD,
	EXPR_LABEL,
	EXPR_INITIALIZER,	// initializer list
	EXPR_IDENTIFIER,	// identifier in initializer
	EXPR_INDEX,		// index in initializer
	EXPR_POS,		// position in initializer
	EXPR_FVALUE,
};

struct expression {
	enum expression_type type;
	int op;
	struct position pos;
	struct symbol *ctype;
	union {
		// EXPR_VALUE
		unsigned long long value;

		// EXPR_FVALUE
		long double fvalue;

		// EXPR_STRING
		struct string *string;

		// EXPR_UNOP, EXPR_PREOP and EXPR_POSTOP
		struct expression *unop;

		// EXPR_SYMBOL, EXPR_TYPE
		struct /* symbol_arg */ {
			struct symbol *symbol;
			struct ident *symbol_name;
		};

		// EXPR_STATEMENT
		struct statement *statement;

		// EXPR_BINOP, EXPR_COMMA, EXPR_COMPARE, EXPR_LOGICAL and EXPR_ASSIGNMENT
		struct /* binop_arg */ {
			struct expression *left, *right;
		};
		// EXPR_DEREF
		struct /* deref_arg */ {
			struct expression *deref;
			struct ident *member;
		};
		// EXPR_CAST and EXPR_SIZEOF
		struct /* cast_arg */ {
			struct symbol *cast_type;
			struct expression *cast_expression;
		};
		// EXPR_CONDITIONAL
		// EXPR_SELECT
		struct /* conditional_expr */ {
			struct expression *conditional, *cond_true, *cond_false;
		};
		// EXPR_CALL
		struct /* call_expr */ {
			struct expression *fn;
			struct expression_list *args;
		};
		// EXPR_BITFIELD
		struct /* bitfield_expr */ {
			unsigned char bitpos, nrbits;
			struct expression *address;
		};
		// EXPR_LABEL
		struct /* label_expr */ {
			struct symbol *label_symbol;
		};
		// EXPR_INITIALIZER
		struct expression_list *expr_list;
		// EXPR_IDENTIFIER
		struct ident *expr_ident;
		// EXPR_INDEX
		struct /* index_expr */ {
			unsigned int idx_from, idx_to;
		};
		// EXPR_POS
		struct /* initpos_expr */ {
			unsigned int init_offset;
			struct symbol *init_sym;
			struct expression *init_expr;
		};
	};
};

/* Constant expression values */
long long get_expression_value(struct expression *);

/* Expression parsing */
struct token *parse_expression(struct token *token, struct expression **tree);
struct token *conditional_expression(struct token *token, struct expression **tree);
struct token *primary_expression(struct token *token, struct expression **tree);
struct token *parens_expression(struct token *token, struct expression **expr, const char *where);
struct token *assignment_expression(struct token *token, struct expression **tree);

extern void check_duplicates(struct symbol *sym);
extern struct symbol *evaluate_symbol(struct symbol *sym);
extern struct symbol *evaluate_statement(struct statement *stmt);
extern struct symbol *evaluate_expression(struct expression *);

extern void expand_symbol(struct symbol *);

static inline struct expression *alloc_expression(struct position pos, int type)
{
	struct expression *expr = __alloc_expression(0);
	expr->type = type;
	expr->pos = pos;
	return expr;
}

static inline struct expression *alloc_const_expression(struct position pos, int value)
{
	struct expression *expr = __alloc_expression(0);
	expr->type = EXPR_VALUE;
	expr->pos = pos;
	expr->value = value;
	expr->ctype = &int_ctype;
	return expr;
}

/* Type name parsing */
struct token *typename(struct token *, struct symbol **);

static inline int lookup_type(struct token *token)
{
	if (token->pos.type == TOKEN_IDENT) {
		struct symbol *sym = lookup_symbol(token->ident, NS_SYMBOL | NS_TYPEDEF);
		return sym && sym->namespace == NS_TYPEDEF;
	}
	return 0;
}

/* Statement parsing */
struct statement *alloc_statement(struct position pos, int type);
struct token *initializer(struct expression **tree, struct token *token);
struct token *compound_statement(struct token *, struct statement *);

/* The preprocessor calls this 'constant_expression()' */
#define constant_expression(token,tree) conditional_expression(token, tree)

/* Cast folding of constant values.. */
void cast_value(struct expression *expr, struct symbol *newtype,
	struct expression *old, struct symbol *oldtype);

#endif
