#ifndef SCOPE_H
#define SCOPE_H

struct scope {
	struct token *token;		/* Scope start information */
	struct symbol_list *symbols;	/* List of symbols in this scope */
	struct scope *next;
};

extern void start_symbol_scope(void);
extern void end_symbol_scope(void);
extern void bind_scope(struct symbol *);

#endif
