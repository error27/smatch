#ifndef PARSE_H
#define PARSE_H

#include "symbol.h"

enum expression_type {
	EXPR_PRIMARY,
	EXPR_BINOP,
	EXPR_DEREF,
	EXPR_PREOP,
	EXPR_POSTOP,
	EXPR_CAST,
	EXPR_SIZEOF,
};

struct expression {
	int type, op;
	struct token *token;
	union {
		struct expression *unop;
		struct binop_arg {
			struct expression *left, *right;
		};
		struct deref_arg {
			struct expression *deref;
			struct token *member;
		};
		struct cast_arg {
			struct symbol *cast_type;
			struct expression *cast_expression;
		};
	};
};

enum statement_type {
	STMT_NONE,
	STMT_EXPRESSION,
	STMT_COMPOUND,
	STMT_IF,
	STMT_RETURN,
};

struct statement {
	int type;
	struct token *token;
	struct statement *next;
	union {
		struct label_arg {
			struct token *label;
			struct statement *label_statement;
		};
		struct expression *expression;
		struct if_statement {
			struct expression *if_conditional;
			struct statement *if_true;
			struct statement *if_false;
		};
		struct compound_struct {
			struct symbol_list *syms;
			struct statement_list *stmts;
		};
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern struct token *statement_list(struct token *, struct statement_list **);
extern void show_expression(struct expression *);
extern void translation_unit(struct token *, struct symbol_list **);

#endif /* PARSE_H */
