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

#include "target.h"

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

struct struct_union_info {
	int advance;
	unsigned long max_align;
	unsigned long bit_size;
	unsigned long offset;
	unsigned long bit_offset;
};

static void examine_one_member(struct symbol *sym, void *_info, int flags)
{
	struct struct_union_info *info = _info;
	unsigned long offset = info->offset;
	unsigned long bit_size;

	examine_symbol_type(sym);
	if (sym->alignment > info->max_align)
		info->max_align = sym->alignment;
	if (info->advance) {
		offset += sym->alignment-1;
		offset &= ~(sym->alignment-1);
		sym->offset = offset;
		info->offset = offset + (sym->bit_size >> 3);
	}
	bit_size = (offset << 3) + sym->bit_size;

	/*
	 * In the case of a union, we want to get the _biggest_ size.
	 * For structures, this will always be true, since the offset
	 * ends up being cumulative.
	 */
	if (bit_size > info->bit_size)
		info->bit_size = bit_size;
}

static void examine_struct_union_type(struct symbol *sym, int advance)
{
	struct struct_union_info info = { advance, 1, 0, 0 };
	unsigned long bit_size, bit_align;

	symbol_iterate(sym->symbol_list, examine_one_member, &info);
	bit_size = info.bit_size;
	bit_align = (info.max_align << 3)-1;
	bit_size = (bit_size + bit_align) & ~bit_align;
	sym->bit_size = bit_size;
	sym->alignment = info.max_align;
}

/*
 * Fill in type size and alignment information for
 * regular SYM_TYPE things.
 */
void examine_symbol_type(struct symbol * sym)
{
	unsigned int bit_size, alignment;
	struct symbol *base_type;
	unsigned long modifiers;

	if (!sym)
		return;

	/* Already done? */
	if (sym->bit_size)
		return;

	switch (sym->type) {
	case SYM_STRUCT:
		examine_struct_union_type(sym, 1);
		return;
	case SYM_UNION:
		examine_struct_union_type(sym, 0);
		return;
	case SYM_PTR:
		if (!sym->bit_size)
			sym->bit_size = BITS_IN_POINTER;
		if (!sym->alignment)
			sym->alignment = POINTER_ALIGNMENT;
		examine_symbol_type(sym->ctype.base_type);
		return;
	case SYM_ENUM:
		if (!sym->bit_size)
			sym->bit_size = BITS_IN_ENUM;
		if (!sym->alignment)
			sym->alignment = ENUM_ALIGNMENT;
		return;
			
	default:
		break;
	}

	base_type = sym->ctype.base_type;
	modifiers = sym->ctype.modifiers;

	if (base_type == &void_type) {
		sym->bit_size = -1;
		return;
	}
		
	if (base_type == &int_type) {
		bit_size = BITS_IN_INT;
		if (modifiers & MOD_LONGLONG) {
			bit_size = BITS_IN_LONGLONG;
		} else if (modifiers & MOD_LONG) {
			bit_size = BITS_IN_LONG;
		} else if (modifiers & MOD_SHORT) {
			bit_size = BITS_IN_SHORT;
		} else if (modifiers & MOD_CHAR) {
			bit_size = BITS_IN_CHAR;
		}
		alignment = bit_size >> 3;
		if (alignment > MAX_INT_ALIGNMENT)
			alignment = MAX_INT_ALIGNMENT;
	} else if (base_type == &fp_type) {
		bit_size = BITS_IN_FLOAT;
		if (modifiers & MOD_LONGLONG) {
			bit_size = BITS_IN_LONGDOUBLE;
		} else if (modifiers & MOD_LONG) {
			bit_size = BITS_IN_DOUBLE;
		}
		alignment = bit_size >> 3;
		if (alignment > MAX_FP_ALIGNMENT)
			alignment = MAX_FP_ALIGNMENT;
	} else if (base_type) {
		examine_symbol_type(base_type);

		bit_size = base_type->bit_size;
		alignment = base_type->alignment;
	} else
		bit_size = 0;

	if (!bit_size) {
		warn(sym->token, "unknown type %d", sym->type);
		return;
	}

	if (sym->type == SYM_ARRAY) {
		int array_size = sym->array_size;
		bit_size *= sym->array_size;
		if (array_size == -1)
			bit_size = -1;
	}

	if (!sym->alignment)
		sym->alignment = alignment;
	sym->bit_size = bit_size;
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
	{ "auto",	NULL,		MOD_AUTO },
	{ "register",	NULL,		MOD_REGISTER },
	{ "static",	NULL,		MOD_STATIC },
	{ "extern",	NULL,		MOD_EXTERN },

	/* Type specifiers */
	{ "void",	&void_type,	0 },
	{ "char",	&int_type,	MOD_CHAR },
	{ "short",	&int_type,	MOD_SHORT },
	{ "int",	&int_type,	0 },
	{ "long",	NULL,		MOD_LONG },
	{ "float",	&fp_type,	0 },
	{ "double",	&fp_type,	MOD_LONG },
	{ "signed",	&int_type,	MOD_SIGNED },
	{ "__signed",	&int_type,	MOD_SIGNED },
	{ "__signed__",	&int_type,	MOD_SIGNED },
	{ "unsigned",	&int_type,	MOD_UNSIGNED },

	/* Type qualifiers */
	{ "const",	NULL,		MOD_CONST },
	{ "__const",	NULL,		MOD_CONST },
	{ "__const__",	NULL,		MOD_CONST },
	{ "volatile",	NULL,		MOD_VOLATILE },

	/* Predeclared types */
	{ "__builtin_va_list", &int_type, 0 },

	/* Typedef.. */
	{ "typedef",	NULL,		MOD_TYPEDEF },

	/* Extended types */
	{ "typeof",	NULL,		MOD_TYPEOF },
	{ "__typeof",	NULL,		MOD_TYPEOF },
	{ "__typeof__",	NULL,		MOD_TYPEOF },

	{ "attribute",	NULL,		MOD_ATTRIBUTE },
	{ "__attribute", NULL,		MOD_ATTRIBUTE },
	{ "__attribute__", NULL,	MOD_ATTRIBUTE },

	{ "struct",	NULL,		MOD_STRUCTOF },
	{ "union",	NULL,		MOD_UNIONOF },
	{ "enum",	NULL,		MOD_ENUMOF },

	/* Ignored for now.. */
	{ "inline",	NULL,		0 },
	{ "__inline",	NULL,		0 },
	{ "__inline__",	NULL,		0 },
	{ "restrict",	NULL,		0 },
	{ "__restrict",	NULL,		0 },

	{ NULL,		NULL,			0 }
};

/*
 * Abstract types
 */
struct symbol	void_type,
		int_type,
		fp_type,
		vector_type,
		bad_type;

/*
 * C types (ie actual instances that the abstract types
 * can map onto)
 */
struct symbol	bool_ctype, void_ctype,
		char_ctype, uchar_ctype,
		short_ctype, ushort_ctype,
		int_ctype, uint_ctype,
		long_ctype, ulong_ctype,
		llong_ctype, ullong_ctype,
		float_ctype, double_ctype, ldouble_ctype,
		string_ctype;

struct ctype_declare {
	struct symbol *ptr;
	unsigned long modifiers;
	unsigned long bit_size;
	unsigned long maxalign;
	struct symbol *base_type;
} ctype_declaration[] = {
	{ &bool_ctype,			     0,			  BITS_IN_INT,	     MAX_INT_ALIGNMENT, &int_type },

	{ &char_ctype,   MOD_SIGNED | MOD_CHAR,  		  BITS_IN_CHAR,	     MAX_INT_ALIGNMENT, &int_type },
	{ &uchar_ctype,  MOD_UNSIGNED | MOD_CHAR,		  BITS_IN_CHAR,	     MAX_INT_ALIGNMENT, &int_type },
	{ &short_ctype,  MOD_SIGNED | MOD_SHORT, 		  BITS_IN_SHORT,     MAX_INT_ALIGNMENT, &int_type },
	{ &ushort_ctype, MOD_UNSIGNED | MOD_SHORT,		  BITS_IN_SHORT,     MAX_INT_ALIGNMENT, &int_type },
	{ &long_ctype,   MOD_SIGNED | MOD_LONG,			  BITS_IN_LONG,	     MAX_INT_ALIGNMENT, &int_type },
	{ &ulong_ctype,  MOD_UNSIGNED | MOD_LONG,		  BITS_IN_LONG,	     MAX_INT_ALIGNMENT, &int_type },
	{ &llong_ctype,  MOD_SIGNED | MOD_LONG | MOD_LONGLONG,    BITS_IN_LONGLONG,  MAX_INT_ALIGNMENT, &int_type },
	{ &ullong_ctype, MOD_UNSIGNED | MOD_LONG | MOD_LONGLONG,  BITS_IN_LONGLONG,  MAX_INT_ALIGNMENT, &int_type },

	{ &float_ctype,  0,					  BITS_IN_FLOAT,     MAX_FP_ALIGNMENT,	&fp_type },
	{ &double_ctype, MOD_LONG,				  BITS_IN_DOUBLE,    MAX_FP_ALIGNMENT,	&fp_type },
	{ &ldouble_ctype,MOD_LONG | MOD_LONGLONG,		  BITS_IN_LONGDOUBLE,MAX_FP_ALIGNMENT,	&fp_type },

	{ &string_ctype,	    0,  BITS_IN_POINTER, POINTER_ALIGNMENT, &char_ctype },
	{ NULL, }
};


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
	struct ctype_declare *ctype;

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
		sym = create_symbol(stream, ptr->name, SYM_NODE);
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
	}

	string_ctype.type = SYM_PTR;
	for (ctype = ctype_declaration ; ctype->ptr; ctype++) {
		struct symbol *sym = ctype->ptr;
		unsigned long bit_size = ctype->bit_size;
		unsigned long alignment = bit_size >> 3;

		if (alignment > ctype->maxalign)
			alignment = ctype->maxalign;
		sym->bit_size = bit_size;
		sym->alignment = alignment;
		sym->ctype.base_type = ctype->base_type;
		sym->ctype.modifiers = ctype->modifiers;
	}
}
