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
enum namespace {
	NS_NONE,
	NS_PREPROCESSOR,
	NS_TYPEDEF,
	NS_STRUCT,
	NS_ENUM,
	NS_LABEL,
};

enum type {
	SYM_NONE,
	SYM_TYPE,
	SYM_PTR,
	SYM_FN,
	SYM_ARRAY,
	SYM_STRUCT,
	SYM_UNION,
	SYM_ENUM,
	SYM_TYPEDEF,
	SYM_MEMBER,
	SYM_BITFIELD,
};

struct symbol {
	enum namespace namespace:8;
	enum type type:8;
	struct token *token;		/* Where this symbol was declared */
	struct symbol *next;		/* Next symbol at this level */
	struct symbol *next_id;		/* Next semantic symbol that shares this identifier */
	unsigned long size;
	unsigned long modifiers;
	struct symbol *base_type;
	struct symbol *children;
	struct statement *stmt;
	struct symbol_list *symbol_list;
};

/* Modifiers */
#define SYM_AUTO	0x0001
#define SYM_REGISTER	0x0002
#define SYM_STATIC	0x0004
#define SYM_EXTERN	0x0008

#define SYM_CONST	0x0010
#define SYM_VOLATILE	0x0020
#define SYM_SIGNED	0x0030
#define SYM_UNSIGNED	0x0040

#define SYM_CHAR	0x0100
#define SYM_SHORT	0x0200
#define SYM_LONG	0x0400
#define SYM_LONGLONG	0x0800

#define SYM_TYPEDEF	0x1000

/* Basic types */
extern struct symbol	void_type,
			int_type,
			fp_type,
			vector_type,
			bad_type;

/* Basic identifiers */
extern struct ident	struct_ident,
			union_ident,
			enum_ident,
			sizeof_ident;

#define symbol_is_typename(sym) ((sym)->type == SYM_TYPE)

extern struct symbol *lookup_symbol(struct ident *, enum namespace);
extern void init_symbols(void);
extern struct symbol *alloc_symbol(struct token *, int type);
extern void show_type(struct symbol *);
extern const char *modifier_string(unsigned long mod);
extern void show_symbol(struct symbol *);
extern void show_type_list(struct symbol *);
extern void show_symbol_list(struct symbol_list *);
extern void add_symbol(struct symbol_list **, struct symbol *);
extern void bind_symbol(struct symbol *, struct ident *, enum namespace);

#endif /* SEMANTIC_H */
