#ifndef PARSE_H
#define PARSE_H
/*
 * Basic parsing data structures. Statements and symbols.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *
 *  Licensed under the Open Software License version 1.1
 */

#include "symbol.h"

enum statement_type {
	STMT_NONE,
	STMT_EXPRESSION,
	STMT_COMPOUND,
	STMT_IF,
	STMT_RETURN,
	STMT_CASE,
	STMT_SWITCH,
	STMT_ITERATOR,
	STMT_LABEL,
	STMT_GOTO,
	STMT_ASM,
};

struct statement {
	enum statement_type type;
	struct position pos;
	struct statement *next;
	union {
		struct label_arg {
			struct symbol *label;
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
			struct symbol *label_identifier;
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
			struct symbol *switch_break, *switch_case;
		};
		struct iterator_struct {
			struct symbol *iterator_break;
			struct symbol *iterator_continue;
			struct statement  *iterator_pre_statement;
			struct expression *iterator_pre_condition;

			struct statement  *iterator_statement;

			struct statement  *iterator_post_statement;
			struct expression *iterator_post_condition;
		};
		struct goto_struct {
			struct symbol *goto_label;
			struct expression *goto_expression;
		};
	};
};

extern struct token *parse_expression(struct token *, struct expression **);
extern struct token *statement_list(struct token *, struct statement_list **);

extern int show_statement(struct statement *);
extern void show_statement_list(struct statement_list *, const char *);
extern int show_expression(struct expression *);
extern void translation_unit(struct token *, struct symbol_list **);

extern struct symbol *ctype_integer(unsigned int spec);
extern struct symbol *ctype_fp(unsigned int spec);

extern int match_string_ident(struct ident *, const char *);

#endif /* PARSE_H */
