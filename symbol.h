#ifndef SEMANTIC_H
#define SEMANTIC_H
/*
 * Basic symbol and namespace definitions.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 */

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
	NS_SYMBOL,
};

enum type {
	SYM_BASETYPE,
	SYM_NODE,
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

struct ctype {
	unsigned long modifiers;
	struct symbol *base_type;
};

struct symbol {
	enum namespace namespace:8;
	enum type type:8;
	struct position pos;		/* Where this symbol was declared */
	struct ident *ident;		/* What identifier this symbol is associated with */
	struct symbol *next_id;		/* Next semantic symbol that shares this identifier */
	struct symbol **id_list;	/* Backpointer to symbol list head */
	union {
		struct preprocessor_sym {
			int busy;
			struct token *expansion;
			struct token *arglist;
		};
		struct ctype_symbol {
			struct symbol *next;		/* Next symbol at this level */
			unsigned long	offset;
			unsigned int	bit_size;
			unsigned int	alignment:24,
					bit_offset:8;
			int	array_size;
			struct ctype ctype;
			struct symbol_list *arguments;
			struct statement *stmt;
			struct symbol_list *symbol_list;
			struct expression *initializer;
			long long value;		/* Initial value */
			int fieldwidth;
			int arg_count:10, variadic:1;
		};
	};
};

/* Modifiers */
#define MOD_AUTO	0x0001
#define MOD_REGISTER	0x0002
#define MOD_STATIC	0x0004
#define MOD_EXTERN	0x0008

#define MOD_STORAGE	(MOD_AUTO | MOD_REGISTER | MOD_STATIC | MOD_EXTERN)

#define MOD_CONST	0x0010
#define MOD_VOLATILE	0x0020
#define MOD_SIGNED	0x0040
#define MOD_UNSIGNED	0x0080

#define MOD_CHAR	0x0100
#define MOD_SHORT	0x0200
#define MOD_LONG	0x0400
#define MOD_LONGLONG	0x0800

#define MOD_TYPEDEF	0x1000
#define MOD_STRUCTOF	0x2000
#define MOD_UNIONOF	0x4000
#define MOD_ENUMOF	0x8000

#define MOD_TYPEOF	0x10000
#define MOD_ATTRIBUTE	0x20000

/* Basic types */
extern struct symbol	void_type,
			int_type,
			fp_type,
			vector_type,
			bad_type;

/* C types */
extern struct symbol	bool_ctype, void_ctype,
			char_ctype, uchar_ctype,
			short_ctype, ushort_ctype,
			int_ctype, uint_ctype,
			long_ctype, ulong_ctype,
			llong_ctype, ullong_ctype,
			float_ctype, double_ctype, ldouble_ctype,
			string_ctype, ptr_ctype;


/* Basic identifiers */
extern struct ident	sizeof_ident,
			alignof_ident,
			__alignof_ident,
			__alignof___ident,
			if_ident,
			else_ident,
			switch_ident,
			case_ident,
			default_ident,
			break_ident,
			continue_ident,
			for_ident,
			while_ident,
			do_ident,
			goto_ident,
			return_ident;

extern struct ident	__asm___ident,
			__asm_ident,
			asm_ident,
			__volatile___ident,
			__volatile_ident,
			volatile_ident,
			__attribute___ident,
			__attribute_ident;

#define symbol_is_typename(sym) ((sym)->type == SYM_TYPE)

extern struct symbol *lookup_symbol(struct ident *, enum namespace);
extern void init_symbols(void);
extern struct symbol *alloc_symbol(struct position, int type);
extern void show_type(struct symbol *);
extern const char *modifier_string(unsigned long mod);
extern void show_symbol(struct symbol *);
extern void show_type_list(struct symbol *);
extern void show_symbol_list(struct symbol_list *, const char *);
extern void add_symbol(struct symbol_list **, struct symbol *);
extern void bind_symbol(struct symbol *, struct ident *, enum namespace);

extern void examine_symbol_type(struct symbol *);
extern void examine_simple_symbol_type(struct symbol *);

#endif /* SEMANTIC_H */
