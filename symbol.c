/*
 * Symbol lookup and handling.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"

struct symbol *lookup_symbol(struct ident *ident, enum namespace ns)
{
	struct symbol *sym;

	for (sym = ident->symbols; sym; sym = sym->next_id) {
		if (sym->namespace == ns)
			return sym;
	}
	return sym;
}

struct symbol *alloc_symbol(struct token *token, int type)
{
	struct symbol *sym = __alloc_symbol(0);
	sym->type = type;
	sym->token = token;
	return sym;
}

void bind_symbol(struct symbol *sym, struct ident *ident, enum namespace ns)
{
	if (sym->id_list) {
		warn(sym->token, "internal error: symbol type already bound");
		return;
	}
	sym->namespace = ns;
	sym->next_id = ident->symbols;
	ident->symbols = sym;
	sym->id_list = &ident->symbols;
	bind_scope(sym);
}

struct symbol *create_symbol(int stream, const char *name, int type)
{
	struct token *token = built_in_token(stream, name);
	struct symbol *sym = alloc_symbol(token, type);
	bind_symbol(sym, token->ident, NS_TYPEDEF);
	return sym;
}

/*
 * Type and storage class keywords need to have the symbols
 * created for them, so that the parser can have enough semantic
 * information to do parsing.
 *
 * "double" == "long float", "long double" == "long long float"
 */
struct sym_init {
	const char *name;
	struct symbol *base_type;
	unsigned int modifiers;
} symbol_init_table[] = {
	/* Storage class */
	{ "auto",	NULL,		SYM_AUTO },
	{ "register",	NULL,		SYM_REGISTER },
	{ "static",	NULL,		SYM_STATIC },
	{ "extern",	NULL,		SYM_EXTERN },

	/* Type specifiers */
	{ "void",	&void_type,	0 },
	{ "char",	&int_type,	SYM_CHAR },
	{ "short",	&int_type,	SYM_SHORT },
	{ "int",	&int_type,	0 },
	{ "long",	NULL,		SYM_LONG },
	{ "float",	&fp_type,	0 },
	{ "double",	&fp_type,	SYM_LONG },
	{ "signed",	&int_type,	SYM_SIGNED },
	{ "__signed",	&int_type,	SYM_SIGNED },
	{ "__signed__",	&int_type,	SYM_SIGNED },
	{ "unsigned",	&int_type,	SYM_UNSIGNED },

	/* Type qualifiers */
	{ "const",	NULL,		SYM_CONST },
	{ "__const",	NULL,		SYM_CONST },
	{ "__const__",	NULL,		SYM_CONST },
	{ "volatile",	NULL,		SYM_VOLATILE },

	/* Predeclared types */
	{ "__builtin_va_list", &int_type, 0 },

	/* Typedef.. */
	{ "typedef",	NULL,		SYM_TYPEDEF },

	/* Extended types */
	{ "typeof",	NULL,		SYM_TYPEOF },
	{ "__typeof",	NULL,		SYM_TYPEOF },
	{ "__typeof__",	NULL,		SYM_TYPEOF },

	{ "attribute",	NULL,		SYM_ATTRIBUTE },
	{ "__attribute", NULL,		SYM_ATTRIBUTE },
	{ "__attribute__", NULL,	SYM_ATTRIBUTE },

	{ "struct",	NULL,		SYM_STRUCTOF },
	{ "union",	NULL,		SYM_UNIONOF },
	{ "enum",	NULL,		SYM_ENUMOF },

	/* Ignored for now.. */
	{ "inline",	NULL,		0 },
	{ "__inline",	NULL,		0 },
	{ "__inline__",	NULL,		0 },
	{ "restrict",	NULL,		0 },
	{ "__restrict",	NULL,		0 },

	{ NULL,		NULL,			0 }
};

struct symbol	void_type,
		int_type,
		fp_type,
		vector_type,
		bad_type;

#define __IDENT(n,str) \
	struct ident n ## _ident = { len: sizeof(str)-1, name: str }
#define IDENT(n) __IDENT(n, #n)

IDENT(struct); IDENT(union); IDENT(enum);
IDENT(sizeof);
IDENT(alignof); IDENT(__alignof); IDENT(__alignof__);
IDENT(if); IDENT(else); IDENT(return);
IDENT(switch); IDENT(case); IDENT(default);
IDENT(break); IDENT(continue);
IDENT(for); IDENT(while); IDENT(do); IDENT(goto);

IDENT(__asm__); IDENT(__asm); IDENT(asm);
IDENT(__volatile__); IDENT(__volatile); IDENT(volatile);
IDENT(__attribute__); IDENT(__attribute);

void init_symbols(void)
{
	int stream = init_stream("builtin", -1);
	struct sym_init *ptr;

	hash_ident(&sizeof_ident);
	hash_ident(&alignof_ident);
	hash_ident(&__alignof_ident);
	hash_ident(&__alignof___ident);
	hash_ident(&if_ident);
	hash_ident(&else_ident);
	hash_ident(&return_ident);
	hash_ident(&switch_ident);
	hash_ident(&case_ident);
	hash_ident(&default_ident);
	hash_ident(&break_ident);
	hash_ident(&continue_ident);
	hash_ident(&for_ident);
	hash_ident(&while_ident);
	hash_ident(&do_ident);
	hash_ident(&goto_ident);
	hash_ident(&__attribute___ident);
	hash_ident(&__attribute_ident);
	hash_ident(&__asm___ident);
	hash_ident(&__asm_ident);
	hash_ident(&asm_ident);
	hash_ident(&__volatile___ident);
	hash_ident(&__volatile_ident);
	hash_ident(&volatile_ident);
	for (ptr = symbol_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_TYPE);
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
	}
}
