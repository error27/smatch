#ifndef PARSE_H
#define PARSE_H

enum expression_type {
	EXPR_PRIMARY,
	EXPR_BINOP,
	EXPR_DEREF,
	EXPR_PREOP,
	EXPR_POSTOP,
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
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern void show_expression(struct expression *);

#endif /* PARSE_H */
