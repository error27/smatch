#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "token.h"

/*
 * An identifier with semantic meaning is a "symbol".
 *
 * There's a 1:n relationship: each symbol is always
 * associated with one identifier, while each identifier
 * can have one or more semantic meanings due to C scope
 * rules.
 *
 * The progression is symbol -> token -> identifier. The
 * token contains the information on where the symbol was
 * declared.
 */
struct symbol {
	struct token *token;		/* Where this symbol was declared */
	struct symbol *next;		/* Next semantic symbol that shares this identifier */
	int type;
	unsigned long size;
	unsigned long modifiers;
	struct symbol *base_type;
	struct symbol *next_type;	/* Next member in this struct/union? */
};

/* Modifiers */
#define SYM_AUTO	0x0001
#define SYM_REGISTER	0x0002
#define SYM_STATIC	0x0004
#define SYM_EXTERN	0x0008

#define SYM_CHAR	0x0010
#define SYM_SHORT	0x0020
#define SYM_LONG	0x0040
#define SYM_LONGLONG	0x0080

#define SYM_SIGNED	0x0100
#define SYM_UNSIGNED	0x0200
#define SYM_CONST	0x0400
#define SYM_VOLATILE	0x0800

/* Basic types */
extern struct symbol	void_type,
			int_type,
			fp_type,
			vector_type;

enum symbol_types {
	SYM_TYPE,
	SYM_PTR,
	SYM_FN,
	SYM_ARRAY,
};

#define symbol_is_typename(sym) ((sym)->type == SYM_TYPE)

extern void init_symbols(void);
extern struct symbol *alloc_symbol(int type);
extern void show_type(struct symbol *);
extern const char *modifier_string(unsigned long mod);

#endif /* SEMANTIC_H */
