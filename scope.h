#ifndef SCOPE_H
#define SCOPE_H
/*
 * Symbol scoping is pretty simple.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *
 *  Licensed under the Open Software License version 1.1
 */

struct scope {
	struct token *token;		/* Scope start information */
	struct symbol_list *symbols;	/* List of symbols in this scope */
	struct scope *next;
};

static inline int toplevel(struct scope *scope)
{
	return scope->next == scope;
}

extern struct scope
		*block_scope,
		*function_scope,
		*file_scope;

extern void start_symbol_scope(void);
extern void end_symbol_scope(void);

extern void start_function_scope(void);
extern void end_function_scope(void);

extern void bind_scope(struct symbol *, struct scope *);

#endif
