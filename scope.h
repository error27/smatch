#ifndef SCOPE_H
#define SCOPE_H
/*
 * Symbol scoping is pretty simple.
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
 */

struct scope {
	struct token *token;		/* Scope start information */
	struct symbol_list *symbols;	/* List of symbols in this scope */
	struct scope *next;
};

extern void start_symbol_scope(void);
extern void end_symbol_scope(void);

extern void start_function_scope(void);
extern void end_function_scope(void);

extern void bind_scope(struct symbol *);

#endif
