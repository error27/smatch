#ifndef PARSE_H
#define PARSE_H

enum expression_type {
	EXPR_UNARY,
	EXPR_BINOP,
	EXPR_IDENT,
};

struct expression {
	int type, op;
	struct token *token;
	union {
		struct expression *unop;
		struct binop_arg {
			struct expression *left, *right;
		};
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern void show_expression(struct expression *);

#endif /* PARSE_H */
