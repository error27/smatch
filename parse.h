#ifndef PARSE_H
#define PARSE_H

enum expression_type {
	EXPR_PRIMARY,
	EXPR_BINOP,
	EXPR_DEREF,
	EXPR_PREOP,
	EXPR_POSTOP,
	EXPR_CAST,
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
	STMT_LABELED,
	STMT_EXPRESSION,
	STMT_COMPOUND,
	STMT_SELECTION,
	STMT_ITERATION,
	STMT_JUMP,
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
		struct statement *compound;
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern struct token *statement_list(struct token *, struct statement **);
extern void show_expression(struct expression *);

#endif /* PARSE_H */
