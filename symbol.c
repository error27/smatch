/*
 * Symbol lookup and handling.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *
 *  Licensed under the Open Software License version 1.1
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
#include "expression.h"

#include "target.h"

/*
 * Secondary symbol list for stuff that needs to be output because it
 * was used. 
 */
struct symbol_list *used_list = NULL;

/*
 * If the symbol is an inline symbol, add it to the list of symbols to parse
 */
void access_symbol(struct symbol *sym)
{
	if (sym->ctype.modifiers & MOD_INLINE) {
		if (!(sym->ctype.modifiers & MOD_ACCESSED)) {
			add_symbol(&used_list, sym);
			sym->ctype.modifiers |= MOD_ACCESSED;
		}
	}
}

struct symbol *lookup_symbol(struct ident *ident, enum namespace ns)
{
	struct symbol *sym;

	for (sym = ident->symbols; sym; sym = sym->next_id) {
		if (sym->namespace == ns) {
			sym->used = 1;
			return sym;
		}
	}
	return sym;
}

struct symbol *alloc_symbol(struct position pos, int type)
{
	struct symbol *sym = __alloc_symbol(0);
	sym->type = type;
	sym->pos = pos;
	return sym;
}

struct struct_union_info {
	unsigned long max_align;
	unsigned long bit_size;
};

/*
 * Unions are easy to lay out ;)
 */
static void lay_out_union(struct symbol *sym, void *_info, int flags)
{
	struct struct_union_info *info = _info;

	examine_symbol_type(sym);
	if (sym->ctype.alignment > info->max_align)
		info->max_align = sym->ctype.alignment;
	if (sym->bit_size > info->bit_size)
		info->bit_size = sym->bit_size;

	sym->offset = 0;
}

/*
 * Structures are a bit more interesting to lay out
 */
static void lay_out_struct(struct symbol *sym, void *_info, int flags)
{
	struct struct_union_info *info = _info;
	unsigned long bit_size, base_size;
	unsigned long align_bit_mask;

	examine_symbol_type(sym);
	if (sym->ctype.alignment > info->max_align)
		info->max_align = sym->ctype.alignment;

	bit_size = info->bit_size;
	base_size = sym->bit_size; 
	align_bit_mask = (sym->ctype.alignment << 3) - 1;

	/*
	 * Bitfields have some very special rules..
	 */
	if (sym->fieldwidth) {
		unsigned long bit_offset = bit_size & align_bit_mask;

		if (bit_offset + sym->fieldwidth > base_size) {
			bit_size = (bit_size + align_bit_mask) & ~align_bit_mask;
			bit_offset = 0;
		}
		sym->offset = (bit_size - bit_offset) >> 3;
		sym->bit_offset = bit_offset;
		info->bit_size = bit_size + sym->fieldwidth;
		return;
	}

	/*
	 * Otherwise, just align it right and add it up..
	 */
	bit_size = (bit_size + align_bit_mask) & ~align_bit_mask;
	sym->offset = bit_size >> 3;

	info->bit_size = bit_size + sym->bit_size;
}

static void examine_struct_union_type(struct symbol *sym, int advance)
{
	struct struct_union_info info = { 1, 0 };
	unsigned long bit_size, bit_align;
	void (*fn)(struct symbol *, void *, int);

	fn = advance ? lay_out_struct : lay_out_union;
	symbol_iterate(sym->symbol_list, fn, &info);

	if (!sym->ctype.alignment)
		sym->ctype.alignment = info.max_align;
	bit_size = info.bit_size;
	bit_align = (sym->ctype.alignment << 3)-1;
	bit_size = (bit_size + bit_align) & ~bit_align;
	sym->bit_size = bit_size;
}

static void examine_array_type(struct symbol *sym)
{
	struct symbol *base_type = sym->ctype.base_type;
	unsigned long bit_size, alignment;

	if (!base_type)
		return;
	examine_symbol_type(base_type);
	bit_size = base_type->bit_size * sym->array_size;
	if (sym->array_size < 0)
		bit_size = -1;
	alignment = base_type->ctype.alignment;
	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;
	sym->bit_size = bit_size;
}

static void examine_bitfield_type(struct symbol *sym)
{
	struct symbol *base_type = sym->ctype.base_type;
	unsigned long bit_size, alignment;

	if (!base_type)
		return;
	examine_symbol_type(base_type);
	bit_size = base_type->bit_size;
	if (sym->fieldwidth > bit_size) {
		warn(sym->pos, "impossible field-width for this type");
		sym->fieldwidth = bit_size;
	}
	alignment = base_type->ctype.alignment;
	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;
	sym->bit_size = bit_size;
}

/*
 * "typeof" will have to merge the types together
 */
void merge_type(struct symbol *sym, struct symbol *base_type)
{
	sym->ctype.as |= base_type->ctype.as;
	sym->ctype.modifiers |= (base_type->ctype.modifiers & ~MOD_STORAGE);
	sym->ctype.context |= base_type->ctype.context;
	sym->ctype.contextmask |= base_type->ctype.contextmask;
	sym->ctype.base_type = base_type->ctype.base_type;
}

/*
 * Fill in type size and alignment information for
 * regular SYM_TYPE things.
 */
struct symbol *examine_symbol_type(struct symbol * sym)
{
	unsigned int bit_size, alignment;
	struct symbol *base_type;
	unsigned long modifiers;

	if (!sym)
		return sym;

	/* Already done? */
	if (sym->bit_size)
		return sym;

	switch (sym->type) {
	case SYM_ARRAY:
		examine_array_type(sym);
		return sym;
	case SYM_STRUCT:
		examine_struct_union_type(sym, 1);
		return sym;
	case SYM_UNION:
		examine_struct_union_type(sym, 0);
		return sym;
	case SYM_PTR:
		if (!sym->bit_size)
			sym->bit_size = BITS_IN_POINTER;
		if (!sym->ctype.alignment)
			sym->ctype.alignment = POINTER_ALIGNMENT;
		base_type = sym->ctype.base_type;
		base_type = examine_symbol_type(base_type);
		if (base_type && base_type->type == SYM_NODE)
			merge_type(sym, base_type);
		return sym;
	case SYM_ENUM:
		if (!sym->bit_size)
			sym->bit_size = BITS_IN_ENUM;
		if (!sym->ctype.alignment)
			sym->ctype.alignment = ENUM_ALIGNMENT;
		return sym;
	case SYM_BITFIELD:
		examine_bitfield_type(sym);
		return sym;
	case SYM_BASETYPE:
		/* Size and alignment had better already be set up */
		return sym;
	case SYM_TYPEOF: {
		struct symbol *base = evaluate_expression(sym->initializer);
		if (base)
			return base;
		break;
	}
	default:
		break;
	}

	/* SYM_NODE - figure out what the type of the node was.. */
	base_type = sym->ctype.base_type;
	modifiers = sym->ctype.modifiers;

	if (base_type) {
		base_type = examine_symbol_type(base_type);
		sym->ctype.base_type = base_type;
		if (base_type && base_type->type == SYM_NODE)
			merge_type(sym, base_type);

		bit_size = base_type->bit_size;
		alignment = base_type->ctype.alignment;
		if (base_type->fieldwidth)
			sym->fieldwidth = base_type->fieldwidth;
	} else
		bit_size = 0;

	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;
	sym->bit_size = bit_size;
	return sym;
}

void check_declaration(struct symbol *sym)
{
	struct symbol *next = sym;

	while ((next = next->next_id) != NULL) {
		if (next->namespace != sym->namespace)
			continue;
		if (sym->scope == next->scope) {
			sym->same_symbol = next;
			return;
		}
		if (sym->ctype.modifiers & next->ctype.modifiers & MOD_EXTERN) {
			sym->same_symbol = next;
			return;
		}
#if 0
		// This may make sense from a warning standpoint:
		//  consider top-level symbols to clash with everything
		//  (but the scoping rules will mean that we actually
		//  _use_ the innermost version)
		if (toplevel(next->scope)) {
			sym->same_symbol = next;
			return;
		}
#endif
	}
}

void bind_symbol(struct symbol *sym, struct ident *ident, enum namespace ns)
{
	struct scope *scope;
	if (sym->id_list) {
		warn(sym->pos, "internal error: symbol type already bound");
		return;
	}
	sym->namespace = ns;
	sym->next_id = ident->symbols;
	ident->symbols = sym;
	sym->id_list = &ident->symbols;

	scope = block_scope;
	if (ns != NS_TYPEDEF && toplevel(scope)) {
		sym->ctype.modifiers |= MOD_TOPLEVEL;
		if (sym->ctype.modifiers & MOD_STATIC)
			scope = file_scope;
	}
	if (ns == NS_LABEL)
		scope = function_scope;
	bind_scope(sym, scope);
}

static struct symbol *create_symbol(int stream, const char *name, int type, int namespace)
{
	struct token *token = built_in_token(stream, name);
	struct symbol *sym = alloc_symbol(token->pos, type);

	sym->ident = token->ident;
	bind_symbol(sym, token->ident, namespace);
	return sym;
}

static int evaluate_constant_p(struct expression *expr)
{
	struct expression *arg;
	struct expression_list *arglist = expr->args;
	int value = 1;

	FOR_EACH_PTR (arglist, arg) {
		if (arg->type != EXPR_VALUE)
			value = 0;
	} END_FOR_EACH_PTR;

	expr->ctype = &int_ctype;
	expr->type = EXPR_VALUE;
	expr->value = value;
	return 1;
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
	int (*evaluate)(struct expression *);
} symbol_init_table[] = {
	/* Storage class */
	{ "auto",	NULL,		MOD_AUTO },
	{ "register",	NULL,		MOD_REGISTER },
	{ "static",	NULL,		MOD_STATIC },
	{ "extern",	NULL,		MOD_EXTERN },

	/* Type specifiers */
	{ "void",	&void_ctype,	0 },
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
	{ "__label__",	&label_type,	MOD_LABEL | MOD_UNSIGNED },

	/* Type qualifiers */
	{ "const",	NULL,		MOD_CONST },
	{ "__const",	NULL,		MOD_CONST },
	{ "__const__",	NULL,		MOD_CONST },
	{ "volatile",	NULL,		MOD_VOLATILE },
	{ "__volatile",	NULL,		MOD_VOLATILE },
	{ "__volatile__", NULL,		MOD_VOLATILE },

	/* Predeclared types */
	{ "__builtin_va_list", &int_type, 0 },

	/* Typedef.. */
	{ "typedef",	NULL,		MOD_TYPEDEF },

	/* Extended types */
	{ "typeof",	NULL,		MOD_TYPEOF },
	{ "__typeof",	NULL,		MOD_TYPEOF },
	{ "__typeof__",	NULL,		MOD_TYPEOF },

#if 0
	{ "attribute",	NULL,		MOD_ATTRIBUTE },
	{ "__attribute", NULL,		MOD_ATTRIBUTE },
#endif
	{ "__attribute__", NULL,	MOD_ATTRIBUTE },

	{ "struct",	NULL,		MOD_STRUCTOF },
	{ "union",	NULL,		MOD_UNIONOF },
	{ "enum",	NULL,		MOD_ENUMOF },

	{ "inline",	NULL,		MOD_INLINE },
	{ "__inline",	NULL,		MOD_INLINE },
	{ "__inline__",	NULL,		MOD_INLINE },

	/* Ignored for now.. */
	{ "restrict",	NULL,		0 },
	{ "__restrict",	NULL,		0 },

	{ NULL,		NULL,		0 }
};

/*
 * Builtin functions
 */
struct sym_init eval_init_table[] = {
	{ "__builtin_constant_p", &int_type, MOD_TOPLEVEL, evaluate_constant_p },

	{ NULL,		NULL,		0 }
};


/*
 * Abstract types
 */
struct symbol	int_type,
		fp_type,
		label_type,
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
		string_ctype, ptr_ctype, label_ctype;

struct ctype_declare {
	struct symbol *ptr;
	unsigned long modifiers;
	unsigned long bit_size;
	unsigned long maxalign;
	struct symbol *base_type;
} ctype_declaration[] = {
	{ &bool_ctype,   0,					  BITS_IN_INT,	     MAX_INT_ALIGNMENT, &int_type },
	{ &void_ctype,   0,					  -1,		     0,			NULL },
	{ &label_ctype,  MOD_LABEL | MOD_UNSIGNED,		  BITS_IN_POINTER,   MAX_INT_ALIGNMENT,	&label_type },

	{ &char_ctype,   MOD_SIGNED | MOD_CHAR,  		  BITS_IN_CHAR,	     MAX_INT_ALIGNMENT, &int_type },
	{ &uchar_ctype,  MOD_UNSIGNED | MOD_CHAR,		  BITS_IN_CHAR,	     MAX_INT_ALIGNMENT, &int_type },
	{ &short_ctype,  MOD_SIGNED | MOD_SHORT, 		  BITS_IN_SHORT,     MAX_INT_ALIGNMENT, &int_type },
	{ &ushort_ctype, MOD_UNSIGNED | MOD_SHORT,		  BITS_IN_SHORT,     MAX_INT_ALIGNMENT, &int_type },
	{ &int_ctype,    MOD_SIGNED,		 		  BITS_IN_INT,	     MAX_INT_ALIGNMENT, &int_type },
	{ &uint_ctype,   MOD_UNSIGNED,				  BITS_IN_INT,	     MAX_INT_ALIGNMENT, &int_type },
	{ &long_ctype,   MOD_SIGNED | MOD_LONG,			  BITS_IN_LONG,	     MAX_INT_ALIGNMENT, &int_type },
	{ &ulong_ctype,  MOD_UNSIGNED | MOD_LONG,		  BITS_IN_LONG,	     MAX_INT_ALIGNMENT, &int_type },
	{ &llong_ctype,  MOD_SIGNED | MOD_LONG | MOD_LONGLONG,    BITS_IN_LONGLONG,  MAX_INT_ALIGNMENT, &int_type },
	{ &ullong_ctype, MOD_UNSIGNED | MOD_LONG | MOD_LONGLONG,  BITS_IN_LONGLONG,  MAX_INT_ALIGNMENT, &int_type },

	{ &float_ctype,  0,					  BITS_IN_FLOAT,     MAX_FP_ALIGNMENT,	&fp_type },
	{ &double_ctype, MOD_LONG,				  BITS_IN_DOUBLE,    MAX_FP_ALIGNMENT,	&fp_type },
	{ &ldouble_ctype,MOD_LONG | MOD_LONGLONG,		  BITS_IN_LONGDOUBLE,MAX_FP_ALIGNMENT,	&fp_type },

	{ &string_ctype,	    0,  BITS_IN_POINTER, POINTER_ALIGNMENT, &char_ctype },
	{ &ptr_ctype,		    0,  BITS_IN_POINTER, POINTER_ALIGNMENT, &void_ctype },
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

__IDENT(pragma, "__pragma__");

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
	hash_ident(&pragma_ident);
	for (ptr = symbol_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_NODE, NS_TYPEDEF);
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
	}

	for (ptr = eval_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_NODE, NS_SYMBOL);
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
		sym->evaluate = ptr->evaluate;
	}

	ptr_ctype.type = SYM_PTR;
	string_ctype.type = SYM_PTR;
	for (ctype = ctype_declaration ; ctype->ptr; ctype++) {
		struct symbol *sym = ctype->ptr;
		unsigned long bit_size = ctype->bit_size;
		unsigned long alignment = bit_size >> 3;

		if (alignment > ctype->maxalign)
			alignment = ctype->maxalign;
		sym->bit_size = bit_size;
		sym->ctype.alignment = alignment;
		sym->ctype.base_type = ctype->base_type;
		sym->ctype.modifiers = ctype->modifiers;
	}
}
