/*
 * Symbol lookup and handling.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
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
		if (sym->namespace & ns) {
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
	int align_size;
};

/*
 * Unions are fairly easy to lay out ;)
 */
static void lay_out_union(struct symbol *sym, struct struct_union_info *info)
{
	examine_symbol_type(sym);

	// Unnamed bitfields do not affect alignment.
	if (sym->ident || !is_bitfield_type(sym)) {
		if (sym->ctype.alignment > info->max_align)
			info->max_align = sym->ctype.alignment;
	}

	if (sym->bit_size > info->bit_size)
		info->bit_size = sym->bit_size;

	sym->offset = 0;
}

/*
 * Structures are a bit more interesting to lay out
 */
static void lay_out_struct(struct symbol *sym, struct struct_union_info *info)
{
	unsigned long bit_size, align_bit_mask;
	int base_size;

	examine_symbol_type(sym);

	// Unnamed bitfields do not affect alignment.
	if (sym->ident || !is_bitfield_type(sym)) {
		if (sym->ctype.alignment > info->max_align)
			info->max_align = sym->ctype.alignment;
	}

	bit_size = info->bit_size;
	base_size = sym->bit_size; 

	/*
	 * Unsized arrays cause us to not align the resulting
	 * structure size
	 */
	if (base_size < 0) {
		info->align_size = 0;
		base_size = 0;
	}

	align_bit_mask = (sym->ctype.alignment << 3) - 1;

	/*
	 * Bitfields have some very special rules..
	 */
	if (is_bitfield_type (sym)) {
		unsigned long bit_offset = bit_size & align_bit_mask;
		int room = base_size - bit_offset;
		// Zero-width fields just fill up the unit.
		int width = sym->fieldwidth ? sym->fieldwidth : (bit_offset ? room : 0);

		if (width > room) {
			bit_size = (bit_size + align_bit_mask) & ~align_bit_mask;
			bit_offset = 0;
		}
		sym->offset = (bit_size - bit_offset) >> 3;
		sym->bit_offset = bit_offset;
		info->bit_size = bit_size + width;
		// warning (sym->pos, "bitfield: offset=%d:%d  size=:%d", sym->offset, sym->bit_offset, width);

		return;
	}

	/*
	 * Otherwise, just align it right and add it up..
	 */
	bit_size = (bit_size + align_bit_mask) & ~align_bit_mask;
	sym->offset = bit_size >> 3;

	info->bit_size = bit_size + base_size;
	// warning (sym->pos, "regular: offset=%d", sym->offset);
}

static void examine_struct_union_type(struct symbol *sym, int advance)
{
	struct struct_union_info info = { 1, 0, 1 };
	unsigned long bit_size, bit_align;
	void (*fn)(struct symbol *, struct struct_union_info *);
	struct symbol *member;

	fn = advance ? lay_out_struct : lay_out_union;
	FOR_EACH_PTR(sym->symbol_list, member) {
		fn(member, &info);
	} END_FOR_EACH_PTR(member);

	if (!sym->ctype.alignment)
		sym->ctype.alignment = info.max_align;
	bit_size = info.bit_size;
	if (info.align_size) {
		bit_align = (sym->ctype.alignment << 3)-1;
		bit_size = (bit_size + bit_align) & ~bit_align;
	}
	sym->bit_size = bit_size;
}

static void examine_array_type(struct symbol *sym)
{
	struct symbol *base_type = sym->ctype.base_type;
	unsigned long bit_size, alignment;

	if (!base_type)
		return;
	examine_symbol_type(base_type);
	bit_size = base_type->bit_size * get_expression_value(sym->array_size);
	if (!sym->array_size || sym->array_size->type != EXPR_VALUE)
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
		warning(sym->pos, "impossible field-width, %d, for this type",
		     sym->fieldwidth);
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
	sym->ctype.in_context += base_type->ctype.in_context;
	sym->ctype.out_context += base_type->ctype.out_context;
	sym->ctype.base_type = base_type->ctype.base_type;
}

static int count_array_initializer(struct expression *expr)
{
	int nr = 0;

	switch (expr->type) {
	case EXPR_STRING:
		nr = expr->string->length;
		break;
	case EXPR_INITIALIZER: {
		struct expression *entry;
		FOR_EACH_PTR(expr->expr_list, entry) {
			switch (entry->type) {
			case EXPR_STRING:
				nr += entry->string->length;
				break;
			case EXPR_INDEX:
				if (entry->idx_to > nr)
					nr = entry->idx_to;
				break;
			default:
				nr++;
			}
		} END_FOR_EACH_PTR(entry);
		break;
	}
	default:
		break;
	}
	return nr;
}

static void examine_node_type(struct symbol *sym)
{
	struct symbol *base_type;
	int bit_size;
	unsigned long alignment, modifiers;

	/* SYM_NODE - figure out what the type of the node was.. */
	base_type = sym->ctype.base_type;
	modifiers = sym->ctype.modifiers;

	bit_size = 0;
	alignment = 0;
	if (!base_type)
		return;

	base_type = examine_symbol_type(base_type);
	sym->ctype.base_type = base_type;
	if (base_type && base_type->type == SYM_NODE)
		merge_type(sym, base_type);

	bit_size = base_type->bit_size;
	alignment = base_type->ctype.alignment;
	if (base_type->fieldwidth)
		sym->fieldwidth = base_type->fieldwidth;

	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;

	/* Unsized array? The size might come from the initializer.. */
	if (bit_size < 0 && base_type->type == SYM_ARRAY && sym->initializer) {
		int count = count_array_initializer(sym->initializer);
		struct symbol *node_type = base_type->ctype.base_type;

		if (node_type && node_type->bit_size >= 0)
			bit_size = node_type->bit_size * count;
	}
	
	sym->bit_size = bit_size;
}

/*
 * Fill in type size and alignment information for
 * regular SYM_TYPE things.
 */
struct symbol *examine_symbol_type(struct symbol * sym)
{
	struct symbol *base_type;

	if (!sym)
		return sym;

	/* Already done? */
	if (sym->bit_size)
		return sym;

	switch (sym->type) {
	case SYM_FN:
	case SYM_NODE:
		examine_node_type(sym);
		return sym;
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
			sym->bit_size = bits_in_pointer;
		if (!sym->ctype.alignment)
			sym->ctype.alignment = pointer_alignment;
		base_type = sym->ctype.base_type;
		base_type = examine_symbol_type(base_type);
		if (base_type && base_type->type == SYM_NODE)
			merge_type(sym, base_type);
		return sym;
	case SYM_ENUM:
		base_type = sym->ctype.base_type;
		base_type = examine_symbol_type(base_type);
		if (base_type == &bad_enum_ctype) {
			warning(sym->pos, "invalid enum type");
			sym->bit_size = -1;
			return sym;
		}
		sym->bit_size = bits_in_enum;
		if (base_type->bit_size > sym->bit_size)
			sym->bit_size = base_type->bit_size;
		sym->ctype.alignment = enum_alignment;
		if (base_type->ctype.alignment > sym->ctype.alignment)
			sym->ctype.alignment = base_type->ctype.alignment;
		return sym;
	case SYM_BITFIELD:
		examine_bitfield_type(sym);
		return sym;
	case SYM_BASETYPE:
		/* Size and alignment had better already be set up */
		return sym;
	case SYM_TYPEOF: {
		struct symbol *base = evaluate_expression(sym->initializer);
		if (base) {
			if (is_bitfield_type(base))
				warning(base->pos, "typeof applied to bitfield type");
			if (base->type == SYM_NODE)
				base = base->ctype.base_type;
			sym->type = SYM_NODE;
			sym->ctype.base_type = base;
		}
		break;
	}
	case SYM_PREPROCESSOR:
		warning(sym->pos, "ctype on preprocessor command? (%s)", show_ident(sym->ident));
		return NULL;
	case SYM_UNINITIALIZED:
		warning(sym->pos, "ctype on uninitialized symbol %p", sym);
		return NULL;
	default:
		warning(sym->pos, "Examining unknown symbol type %d", sym->type);
		break;
	}
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
		warning(sym->pos, "internal error: symbol type already bound");
		return;
	}
	if (ident->reserved && (ns & (NS_TYPEDEF | NS_STRUCT | NS_LABEL | NS_SYMBOL))) {
		warning(sym->pos, "Trying to use reserved word '%s' as identifier", show_ident(ident));
		return;
	}
	sym->namespace = ns;
	sym->next_id = ident->symbols;
	ident->symbols = sym;
	sym->id_list = &ident->symbols;
	if (sym->ident && sym->ident != ident)
		warning(sym->pos, "Symbol '%s' already bound", show_ident(sym->ident));
	sym->ident = ident;

	scope = block_scope;
	if (ns == NS_SYMBOL && toplevel(scope)) {
		sym->ctype.modifiers |= MOD_TOPLEVEL | MOD_ADDRESSABLE;
		if (sym->ctype.modifiers & MOD_STATIC)
			scope = file_scope;
	}
	if (ns == NS_LABEL)
		scope = function_scope;
	bind_scope(sym, scope);
}

struct symbol *create_symbol(int stream, const char *name, int type, int namespace)
{
	struct token *token = built_in_token(stream, name);
	struct symbol *sym = alloc_symbol(token->pos, type);

	bind_symbol(sym, token->ident, namespace);
	return sym;
}

static int evaluate_constant_p(struct expression *expr)
{
	expr->ctype = &int_ctype;
	return 1;
}

static int expand_constant_p(struct expression *expr)
{
	struct expression *arg;
	struct expression_list *arglist = expr->args;
	int value = 1;

	FOR_EACH_PTR (arglist, arg) {
		if (arg->type != EXPR_VALUE && arg->type != EXPR_FVALUE)
			value = 0;
	} END_FOR_EACH_PTR(arg);

	expr->type = EXPR_VALUE;
	expr->value = value;
	return 0;
}

/*
 * __builtin_warning() has type "int" and always returns 1,
 * so that you can use it in conditionals or whatever
 */
static int evaluate_warning(struct expression *expr)
{
	expr->ctype = &int_ctype;
	return 1;
}

static int expand_warning(struct expression *expr)
{
	struct expression *arg;
	struct expression_list *arglist = expr->args;

	FOR_EACH_PTR (arglist, arg) {
		/*
		 * Constant strings get printed out as a warning. By the
		 * time we get here, the EXPR_STRING has been fully 
		 * evaluated, so by now it's an anonymous symbol with a
		 * string initializer.
		 *
		 * Just for the heck of it, allow any constant string
		 * symbol.
		 */
		if (arg->type == EXPR_SYMBOL) {
			struct symbol *sym = arg->symbol;
			if (sym->initializer && sym->initializer->type == EXPR_STRING) {
				struct string *string = sym->initializer->string;
				warning(expr->pos, "%*s", string->length-1, string->data);
			}
			continue;
		}

		/*
		 * Any other argument is a conditional. If it's
		 * non-constant, or it is false, we exit and do
		 * not print any warning.
		 */
		if (arg->type != EXPR_VALUE)
			goto out;
		if (!arg->value)
			goto out;
	} END_FOR_EACH_PTR(arg);
out:
	expr->type = EXPR_VALUE;
	expr->value = 1;
	return 0;
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
	struct symbol_op *op;
} symbol_init_table[] = {
	/* Storage class */
	{ "auto",	NULL,		MOD_AUTO },
	{ "register",	NULL,		MOD_REGISTER },
	{ "static",	NULL,		MOD_STATIC },
	{ "extern",	NULL,		MOD_EXTERN },

	/* Type specifiers */
	{ "void",	&void_ctype,	0 },
	{ "char",	NULL,		MOD_CHAR },
	{ "short",	NULL,		MOD_SHORT },
	{ "int",	&int_type,	0 },
	{ "long",	NULL,		MOD_LONG },
	{ "float",	&fp_type,	0 },
	{ "double",	&fp_type,	MOD_LONG },
	{ "signed",	NULL,		MOD_SIGNED | MOD_EXPLICITLY_SIGNED },
	{ "__signed",	NULL,		MOD_SIGNED | MOD_EXPLICITLY_SIGNED },
	{ "__signed__",	NULL,		MOD_SIGNED | MOD_EXPLICITLY_SIGNED },
	{ "unsigned",	NULL,		MOD_UNSIGNED },
	{ "__label__",	&label_ctype,	MOD_LABEL | MOD_UNSIGNED },

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
#endif
	{ "__attribute", NULL,		MOD_ATTRIBUTE },
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

static struct symbol_op constant_p_op = {
	.evaluate = evaluate_constant_p,
	.expand = expand_constant_p
};

static struct symbol_op warning_op = {
	.evaluate = evaluate_warning,
	.expand = expand_warning
};

/*
 * Builtin functions
 */
static struct symbol builtin_fn_type = { .type = SYM_FN /* , .variadic =1 */ };
static struct sym_init eval_init_table[] = {
	{ "__builtin_constant_p", &builtin_fn_type, MOD_TOPLEVEL, &constant_p_op },
	{ "__builtin_warning", &builtin_fn_type, MOD_TOPLEVEL, &warning_op },
	{ NULL,		NULL,		0 }
};


/*
 * Abstract types
 */
struct symbol	int_type,
		fp_type,
		vector_type,
		bad_type;

/*
 * C types (ie actual instances that the abstract types
 * can map onto)
 */
struct symbol	bool_ctype, void_ctype, type_ctype,
		char_ctype, schar_ctype, uchar_ctype,
		short_ctype, sshort_ctype, ushort_ctype,
		int_ctype, sint_ctype, uint_ctype,
		long_ctype, slong_ctype, ulong_ctype,
		llong_ctype, sllong_ctype, ullong_ctype,
		float_ctype, double_ctype, ldouble_ctype,
		string_ctype, ptr_ctype, lazy_ptr_ctype,
		incomplete_ctype, label_ctype, bad_enum_ctype;


#define __INIT_IDENT(str, res) { .len = sizeof(str)-1, .name = str, .reserved = res }
#define __IDENT(n,str,res) \
	struct ident n  = __INIT_IDENT(str,res)

#include "ident-list.h"

void init_symbols(void)
{
	int stream = init_stream("builtin", -1, includepath);
	struct sym_init *ptr;

#define __IDENT(n,str,res) \
	hash_ident(&n)
#include "ident-list.h"

	for (ptr = symbol_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_NODE, NS_TYPEDEF);
		sym->ident->reserved = 1;
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
	}

	builtin_fn_type.variadic = 1;
	for (ptr = eval_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_NODE, NS_SYMBOL);
		sym->ctype.base_type = ptr->base_type;
		sym->ctype.modifiers = ptr->modifiers;
		sym->op = ptr->op;
	}
}

#define MOD_ESIGNED (MOD_SIGNED | MOD_EXPLICITLY_SIGNED)
#define MOD_LL (MOD_LONG | MOD_LONGLONG)
static const struct ctype_declare {
	struct symbol *ptr;
	enum type type;
	unsigned long modifiers;
	int *bit_size;
	int *maxalign;
	struct symbol *base_type;
} ctype_declaration[] = {
	{ &bool_ctype,	    SYM_BASETYPE, 0,			    &bits_in_int,	     &max_int_alignment, &int_type },
	{ &void_ctype,	    SYM_BASETYPE, 0,			    NULL,		     NULL,		 NULL },
	{ &type_ctype,	    SYM_BASETYPE, MOD_TYPE,		    NULL,		     NULL,		 NULL },
	{ &incomplete_ctype,SYM_BASETYPE, 0,			    NULL,		     NULL,		 NULL },
	{ &bad_enum_ctype,	SYM_BAD, 0,			    NULL,		     NULL,		 NULL },

	{ &char_ctype,	    SYM_BASETYPE, MOD_SIGNED | MOD_CHAR,    &bits_in_char,	     &max_int_alignment, &int_type },
	{ &schar_ctype,	    SYM_BASETYPE, MOD_ESIGNED | MOD_CHAR,   &bits_in_char,	     &max_int_alignment, &int_type },
	{ &uchar_ctype,	    SYM_BASETYPE, MOD_UNSIGNED | MOD_CHAR,  &bits_in_char,	     &max_int_alignment, &int_type },
	{ &short_ctype,	    SYM_BASETYPE, MOD_SIGNED | MOD_SHORT,   &bits_in_short,	     &max_int_alignment, &int_type },
	{ &sshort_ctype,    SYM_BASETYPE, MOD_ESIGNED | MOD_SHORT,  &bits_in_short,	     &max_int_alignment, &int_type },
	{ &ushort_ctype,    SYM_BASETYPE, MOD_UNSIGNED | MOD_SHORT, &bits_in_short,	     &max_int_alignment, &int_type },
	{ &int_ctype,	    SYM_BASETYPE, MOD_SIGNED,		    &bits_in_int,	     &max_int_alignment, &int_type },
	{ &sint_ctype,	    SYM_BASETYPE, MOD_ESIGNED,		    &bits_in_int,	     &max_int_alignment, &int_type },
	{ &uint_ctype,	    SYM_BASETYPE, MOD_UNSIGNED,		    &bits_in_int,	     &max_int_alignment, &int_type },
	{ &long_ctype,	    SYM_BASETYPE, MOD_SIGNED | MOD_LONG,    &bits_in_long,	     &max_int_alignment, &int_type },
	{ &slong_ctype,	    SYM_BASETYPE, MOD_ESIGNED | MOD_LONG,   &bits_in_long,	     &max_int_alignment, &int_type },
	{ &ulong_ctype,	    SYM_BASETYPE, MOD_UNSIGNED | MOD_LONG,  &bits_in_long,	     &max_int_alignment, &int_type },
	{ &llong_ctype,	    SYM_BASETYPE, MOD_SIGNED | MOD_LL,	    &bits_in_longlong,       &max_int_alignment, &int_type },
	{ &sllong_ctype,    SYM_BASETYPE, MOD_ESIGNED | MOD_LL,	    &bits_in_longlong,       &max_int_alignment, &int_type },
	{ &ullong_ctype,    SYM_BASETYPE, MOD_UNSIGNED | MOD_LL,    &bits_in_longlong,       &max_int_alignment, &int_type },

	{ &float_ctype,	    SYM_BASETYPE,  0,			    &bits_in_float,          &max_fp_alignment,  &fp_type },
	{ &double_ctype,    SYM_BASETYPE, MOD_LONG,		    &bits_in_double,         &max_fp_alignment,  &fp_type },
	{ &ldouble_ctype,   SYM_BASETYPE, MOD_LONG | MOD_LONGLONG,  &bits_in_longdouble,     &max_fp_alignment,  &fp_type },

	{ &string_ctype,    SYM_PTR,	  0,			    &bits_in_pointer,        &pointer_alignment, &char_ctype },
	{ &ptr_ctype,	    SYM_PTR,	  0,			    &bits_in_pointer,        &pointer_alignment, &void_ctype },
	{ &label_ctype,	    SYM_PTR,	  0,			    &bits_in_pointer,        &pointer_alignment, &void_ctype },
	{ &lazy_ptr_ctype,  SYM_PTR,	  0,			    &bits_in_pointer,        &pointer_alignment, &void_ctype },
	{ NULL, }
};
#undef MOD_LL
#undef MOD_ESIGNED

void init_ctype(void)
{
	const struct ctype_declare *ctype;

	for (ctype = ctype_declaration ; ctype->ptr; ctype++) {
		struct symbol *sym = ctype->ptr;
		unsigned long bit_size = ctype->bit_size ? *ctype->bit_size : -1;
		unsigned long maxalign = ctype->maxalign ? *ctype->maxalign : 0;
		unsigned long alignment = bit_size >> 3;

		if (alignment > maxalign)
			alignment = maxalign;
		sym->type = ctype->type;
		sym->bit_size = bit_size;
		sym->ctype.alignment = alignment;
		sym->ctype.base_type = ctype->base_type;
		sym->ctype.modifiers = ctype->modifiers;
	}
}
