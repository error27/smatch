#ifndef PARSE_H
#define PARSE_H
/*
 * Basic parsing data structures. Statements and symbols.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 */

#include "symbol.h"

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
	STMT_ITERATOR,
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
		struct iterator_struct {
			struct statement  *iterator_pre_statement;
			struct expression *iterator_pre_condition;

			struct statement  *iterator_statement;

			struct statement  *iterator_post_statement;
			struct expression *iterator_post_condition;
		};
		struct goto_struct {
			struct token *goto_label;
			struct expression *goto_expression;
		};
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern struct token *statement_list(struct token *, struct statement_list **);

extern void show_statement(struct statement *);
extern void show_statement_list(struct statement_list *, const char *);
extern void show_expression(struct expression *);
extern void translation_unit(struct token *, struct symbol_list **);

extern struct symbol *ctype_integer(unsigned int spec);
extern struct symbol *ctype_fp(unsigned int spec);

#endif /* PARSE_H */
