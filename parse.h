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
	EXPR_CONDITIONAL,
	EXPR_STATEMENT,
};

struct expression {
	int type, op;
	struct token *token;
	union {
		struct expression *unop;
		struct statement *statement;
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
		struct conditional_expr {
			struct expression *conditional, *cond_true, *cond_false;
		};
		struct statement_struct {
			struct symbol_list *syms;
			struct statement_list *stmts;
		};
	};
};

enum statement_type {
	STMT_NONE,
	STMT_EXPRESSION,
	STMT_COMPOUND,
	STMT_IF,
	STMT_RETURN,
	STMT_BREAK,
	STMT_CONTINUE,
	STMT_CASE,
	STMT_SWITCH,
	STMT_FOR,
	STMT_WHILE,
	STMT_DO,
	STMT_LABEL,
	STMT_GOTO,
	STMT_ASM,
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
		struct labeled_struct {
			struct token *label_identifier;
			struct statement *label_statement;
		};
		struct case_struct {
			struct expression *case_expression;
			struct expression *case_to;
			struct statement *case_statement;
		};
		struct switch_struct {
			struct expression *switch_expression;
			struct statement *switch_statement;
		};
		struct iteration_struct {
			struct expression *e1, *e2, *e3;
			struct statement *iterate;
		};
		struct goto_struct {
			struct token *goto_label;
			struct expression *goto_expression;
		};
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern struct token *statement_list(struct token *, struct statement_list **);
extern void show_expression(struct expression *);
extern void translation_unit(struct token *, struct symbol_list **);

#endif /* PARSE_H */
