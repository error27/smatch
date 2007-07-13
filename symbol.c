/*
 * Symbol lookup and handling.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "allocate.h"
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
struct symbol_list *translation_unit_used_list = NULL;

/*
 * If the symbol is an inline symbol, add it to the list of symbols to parse
 */
void access_symbol(struct symbol *sym)
{
	if (sym->ctype.modifiers & MOD_INLINE) {
		if (!(sym->ctype.modifiers & MOD_ACCESSED)) {
			add_symbol(&translation_unit_used_list, sym);
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

struct context *alloc_context(void)
{
	return __alloc_context(0);
}

struct symbol *alloc_symbol(struct position pos, int type)
{
	struct symbol *sym = __alloc_symbol(0);
	sym->type = type;
	sym->pos = pos;
	sym->endpos.type = 0;
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

static int bitfield_base_size(struct symbol *sym)
{
	if (sym->type == SYM_NODE)
		sym = sym->ctype.base_type;
	if (sym->type == SYM_BITFIELD)
		sym = sym->ctype.base_type;
	return sym->bit_size;
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
		int room = bitfield_base_size(sym) - bit_offset;
		// Zero-width fields just fill up the unit.
		int width = base_size ? : (bit_offset ? room : 0);

		if (width > room) {
			bit_size = (bit_size + align_bit_mask) & ~align_bit_mask;
			bit_offset = 0;
		}
		sym->offset = (bit_size - bit_offset) >> 3;
		sym->bit_offset = bit_offset;
		sym->ctype.base_type->bit_offset = bit_offset;
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

static struct symbol * examine_struct_union_type(struct symbol *sym, int advance)
{
	struct struct_union_info info = {
		.max_align = 1,
		.bit_size = 0,
		.align_size = 1
	};
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
	return sym;
}

static struct symbol *examine_base_type(struct symbol *sym)
{
	struct symbol *base_type;

	/* Check the base type */
	base_type = examine_symbol_type(sym->ctype.base_type);
	if (!base_type || base_type->type == SYM_PTR)
		return base_type;
	sym->ctype.as |= base_type->ctype.as;
	sym->ctype.modifiers |= base_type->ctype.modifiers & MOD_PTRINHERIT;
	concat_ptr_list((struct ptr_list *)base_type->ctype.contexts,
			(struct ptr_list **)&sym->ctype.contexts);
	if (base_type->type == SYM_NODE) {
		base_type = base_type->ctype.base_type;
		sym->ctype.base_type = base_type;
	}
	return base_type;
}

static struct symbol * examine_array_type(struct symbol *sym)
{
	struct symbol *base_type = examine_base_type(sym);
	unsigned long bit_size, alignment;

	if (!base_type)
		return sym;
	bit_size = base_type->bit_size * get_expression_value(sym->array_size);
	if (!sym->array_size || sym->array_size->type != EXPR_VALUE)
		bit_size = -1;
	alignment = base_type->ctype.alignment;
	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;
	sym->bit_size = bit_size;
	return sym;
}

static struct symbol *examine_bitfield_type(struct symbol *sym)
{
	struct symbol *base_type = examine_base_type(sym);
	unsigned long bit_size, alignment, modifiers;

	if (!base_type)
		return sym;
	bit_size = base_type->bit_size;
	if (sym->bit_size > bit_size)
		warning(sym->pos, "impossible field-width, %d, for this type",  sym->bit_size);

	alignment = base_type->ctype.alignment;
	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;
	modifiers = base_type->ctype.modifiers;

	/* Bitfields are unsigned, unless the base type was explicitly signed */
	if (!(modifiers & MOD_EXPLICITLY_SIGNED))
		modifiers = (modifiers & ~MOD_SIGNED) | MOD_UNSIGNED;
	sym->ctype.modifiers |= modifiers & MOD_SIGNEDNESS;
	return sym;
}

/*
 * "typeof" will have to merge the types together
 */
void merge_type(struct symbol *sym, struct symbol *base_type)
{
	sym->ctype.as |= base_type->ctype.as;
	sym->ctype.modifiers |= (base_type->ctype.modifiers & ~MOD_STORAGE);
	concat_ptr_list((struct ptr_list *)base_type->ctype.contexts,
	                (struct ptr_list **)&sym->ctype.contexts);
	sym->ctype.base_type = base_type->ctype.base_type;
	if (sym->ctype.base_type->type == SYM_NODE)
		merge_type(sym, sym->ctype.base_type);
}

static int count_array_initializer(struct symbol *t, struct expression *expr)
{
	int nr = 0;
	int is_char = 0;

	/*
	 * Arrays of character types are special; they can be initialized by
	 * string literal _or_ by string literal in braces.  The latter means
	 * that with T x[] = {<string literal>} number of elements in x depends
	 * on T - if it's a character type, we get the length of string literal
	 * (including NUL), otherwise we have one element here.
	 */
	if (t->ctype.base_type == &int_type && t->ctype.modifiers & MOD_CHAR)
		is_char = 1;

	switch (expr->type) {
	case EXPR_INITIALIZER: {
		struct expression *entry;
		int count = 0;
		int str_len = 0;
		FOR_EACH_PTR(expr->expr_list, entry) {
			count++;
			switch (entry->type) {
			case EXPR_INDEX:
				if (entry->idx_to >= nr)
					nr = entry->idx_to+1;
				break;
			case EXPR_STRING:
				if (is_char)
					str_len = entry->string->length;
			default:
				nr++;
			}
		} END_FOR_EACH_PTR(entry);
		if (count == 1 && str_len)
			nr = str_len;
		break;
	}
	case EXPR_STRING:
		if (is_char)
			nr = expr->string->length;
	default:
		break;
	}
	return nr;
}

static struct symbol * examine_node_type(struct symbol *sym)
{
	struct symbol *base_type = examine_base_type(sym);
	int bit_size;
	unsigned long alignment, modifiers;

	/* SYM_NODE - figure out what the type of the node was.. */
	modifiers = sym->ctype.modifiers;

	bit_size = 0;
	alignment = 0;
	if (!base_type)
		return sym;

	bit_size = base_type->bit_size;
	alignment = base_type->ctype.alignment;

	/* Pick up signedness information into the node */
	sym->ctype.modifiers |= (MOD_SIGNEDNESS & base_type->ctype.modifiers);

	if (!sym->ctype.alignment)
		sym->ctype.alignment = alignment;

	/* Unsized array? The size might come from the initializer.. */
	if (bit_size < 0 && base_type->type == SYM_ARRAY && sym->initializer) {
		struct symbol *node_type = base_type->ctype.base_type;
		int count = count_array_initializer(node_type, sym->initializer);

		if (node_type && node_type->bit_size >= 0)
			bit_size = node_type->bit_size * count;
	}
	
	sym->bit_size = bit_size;
	return sym;
}

static struct symbol *examine_enum_type(struct symbol *sym)
{
	struct symbol *base_type = examine_base_type(sym);

	sym->ctype.modifiers |= (base_type->ctype.modifiers & MOD_SIGNEDNESS);
	sym->bit_size = bits_in_enum;
	if (base_type->bit_size > sym->bit_size)
		sym->bit_size = base_type->bit_size;
	sym->ctype.alignment = enum_alignment;
	if (base_type->ctype.alignment > sym->ctype.alignment)
		sym->ctype.alignment = base_type->ctype.alignment;
	return sym;
}

static struct symbol *examine_pointer_type(struct symbol *sym)
{
	/*
	 * We need to set the pointer size first, and
	 * examine the thing we point to only afterwards.
	 * That's because this pointer type may end up
	 * being needed for the base type size evaluation.
	 */
	if (!sym->bit_size)
		sym->bit_size = bits_in_pointer;
	if (!sym->ctype.alignment)
		sym->ctype.alignment = pointer_alignment;
	return sym;
}

/*
 * Fill in type size and alignment information for
 * regular SYM_TYPE things.
 */
struct symbol *examine_symbol_type(struct symbol * sym)
{
	if (!sym)
		return sym;

	/* Already done? */
	if (sym->examined)
		return sym;
	sym->examined = 1;

	switch (sym->type) {
	case SYM_FN:
	case SYM_NODE:
		return examine_node_type(sym);
	case SYM_ARRAY:
		return examine_array_type(sym);
	case SYM_STRUCT:
		return examine_struct_union_type(sym, 1);
	case SYM_UNION:
		return examine_struct_union_type(sym, 0);
	case SYM_PTR:
		return examine_pointer_type(sym);
	case SYM_ENUM:
		return examine_enum_type(sym);
	case SYM_BITFIELD:
		return examine_bitfield_type(sym);
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
			sym->ctype.modifiers = 0;
			sym->ctype.base_type = base;
			return examine_node_type(sym);
		}
		break;
	}
	case SYM_PREPROCESSOR:
		sparse_error(sym->pos, "ctype on preprocessor command? (%s)", show_ident(sym->ident));
		return NULL;
	case SYM_UNINITIALIZED:
		sparse_error(sym->pos, "ctype on uninitialized symbol %p", sym);
		return NULL;
	case SYM_RESTRICT:
		examine_base_type(sym);
		return sym;
	case SYM_FOULED:
		examine_base_type(sym);
		return sym;
	default:
		sparse_error(sym->pos, "Examining unknown symbol type %d", sym->type);
		break;
	}
	return sym;
}

const char* get_type_name(enum type type)
{
	const char *type_lookup[] = {
	[SYM_UNINITIALIZED] = "uninitialized",
	[SYM_PREPROCESSOR] = "preprocessor",
	[SYM_BASETYPE] = "basetype",
	[SYM_NODE] = "node",
	[SYM_PTR] = "pointer",
	[SYM_FN] = "function",
	[SYM_ARRAY] = "array",
	[SYM_STRUCT] = "struct",
	[SYM_UNION] = "union",
	[SYM_ENUM] = "enum",
	[SYM_TYPEDEF] = "typedef",
	[SYM_TYPEOF] = "typeof",
	[SYM_MEMBER] = "member",
	[SYM_BITFIELD] = "bitfield",
	[SYM_LABEL] = "label",
	[SYM_RESTRICT] = "restrict",
	[SYM_FOULED] = "fouled",
	[SYM_KEYWORD] = "keyword",
	[SYM_BAD] = "bad"};

	if (type <= SYM_BAD)
		return type_lookup[type];
	else
		return NULL;
}

struct symbol *examine_pointer_target(struct symbol *sym)
{
	return examine_base_type(sym);
}

static struct symbol_list *restr, *fouled;

void create_fouled(struct symbol *type)
{
	if (type->bit_size < bits_in_int) {
		struct symbol *new = alloc_symbol(type->pos, type->type);
		*new = *type;
		new->bit_size = bits_in_int;
		new->type = SYM_FOULED;
		new->ctype.base_type = type;
		add_symbol(&restr, type);
		add_symbol(&fouled, new);
	}
}

struct symbol *befoul(struct symbol *type)
{
	struct symbol *t1, *t2;
	while (type->type == SYM_NODE)
		type = type->ctype.base_type;
	PREPARE_PTR_LIST(restr, t1);
	PREPARE_PTR_LIST(fouled, t2);
	for (;;) {
		if (t1 == type)
			return t2;
		if (!t1)
			break;
		NEXT_PTR_LIST(t1);
		NEXT_PTR_LIST(t2);
	}
	FINISH_PTR_LIST(t2);
	FINISH_PTR_LIST(t1);
	return NULL;
}

void check_declaration(struct symbol *sym)
{
	int warned = 0;
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

		if (!Wshadow || warned)
			continue;
		if (get_sym_type(next) == SYM_FN)
			continue;
		warned = 1;
		warning(sym->pos, "symbol '%s' shadows an earlier one", show_ident(sym->ident));
		info(next->pos, "originally declared here");
	}
}

void bind_symbol(struct symbol *sym, struct ident *ident, enum namespace ns)
{
	struct scope *scope;
	if (sym->id_list) {
		sparse_error(sym->pos, "internal error: symbol type already bound");
		return;
	}
	if (ident->reserved && (ns & (NS_TYPEDEF | NS_STRUCT | NS_LABEL | NS_SYMBOL))) {
		sparse_error(sym->pos, "Trying to use reserved word '%s' as identifier", show_ident(ident));
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
		unsigned mod = MOD_ADDRESSABLE | MOD_TOPLEVEL;
		scope = global_scope;
		if (sym->ctype.modifiers & MOD_STATIC) {
			scope = file_scope;
			mod = MOD_TOPLEVEL;
		}
		sym->ctype.modifiers |= mod;
	}
	if (ns == NS_MACRO)
		scope = file_scope;
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

static int evaluate_to_integer(struct expression *expr)
{
	expr->ctype = &int_ctype;
	return 1;
}

static int evaluate_expect(struct expression *expr)
{
	/* Should we evaluate it to return the type of the first argument? */
	expr->ctype = &int_ctype;
	return 1;
}

static int arguments_choose(struct expression *expr)
{
	struct expression_list *arglist = expr->args;
	struct expression *arg;
	int i = 0;

	FOR_EACH_PTR (arglist, arg) {
		if (!evaluate_expression(arg))
			return 0;
		i++;
	} END_FOR_EACH_PTR(arg);
	if (i < 3) {
		sparse_error(expr->pos,
			     "not enough arguments for __builtin_choose_expr");
		return 0;
	} if (i > 3) {
		sparse_error(expr->pos,
			     "too many arguments for __builtin_choose_expr");
		return 0;
	}
	return 1;
}

static int evaluate_choose(struct expression *expr)
{
	struct expression_list *list = expr->args;
	struct expression *arg, *args[3];
	int n = 0;

	/* there will be exactly 3; we'd already verified that */
	FOR_EACH_PTR(list, arg) {
		args[n++] = arg;
	} END_FOR_EACH_PTR(arg);

	*expr = get_expression_value(args[0]) ? *args[1] : *args[2];

	return 1;
}

static int expand_expect(struct expression *expr, int cost)
{
	struct expression *arg = first_ptr_list((struct ptr_list *) expr->args);

	if (arg)
		*expr = *arg;
	return 0;
}

/*
 * __builtin_warning() has type "int" and always returns 1,
 * so that you can use it in conditionals or whatever
 */
static int expand_warning(struct expression *expr, int cost)
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
	expr->taint = 0;
	return 0;
}

/*
 * Type and storage class keywords need to have the symbols
 * created for them, so that the parser can have enough semantic
 * information to do parsing.
 *
 * "double" == "long float", "long double" == "long long float"
 */
static struct sym_init {
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
	{ "_Bool",	&bool_ctype,	MOD_UNSIGNED },

	/* Predeclared types */
	{ "__builtin_va_list", &int_type, 0 },

	{ NULL,		NULL,		0 }
};

static struct symbol_op constant_p_op = {
	.evaluate = evaluate_to_integer,
	.expand = expand_constant_p
};

static struct symbol_op safe_p_op = {
	.evaluate = evaluate_to_integer,
	.expand = expand_safe_p
};

static struct symbol_op warning_op = {
	.evaluate = evaluate_to_integer,
	.expand = expand_warning
};

static struct symbol_op expect_op = {
	.evaluate = evaluate_expect,
	.expand = expand_expect
};

static struct symbol_op choose_op = {
	.evaluate = evaluate_choose,
	.args = arguments_choose,
};

/*
 * Builtin functions
 */
static struct symbol builtin_fn_type = { .type = SYM_FN /* , .variadic =1 */ };
static struct sym_init eval_init_table[] = {
	{ "__builtin_constant_p", &builtin_fn_type, MOD_TOPLEVEL, &constant_p_op },
	{ "__builtin_safe_p", &builtin_fn_type, MOD_TOPLEVEL, &safe_p_op },
	{ "__builtin_warning", &builtin_fn_type, MOD_TOPLEVEL, &warning_op },
	{ "__builtin_expect", &builtin_fn_type, MOD_TOPLEVEL, &expect_op },
	{ "__builtin_choose_expr", &builtin_fn_type, MOD_TOPLEVEL, &choose_op },
	{ NULL,		NULL,		0 }
};


/*
 * Abstract types
 */
struct symbol	int_type,
		fp_type;

/*
 * C types (i.e. actual instances that the abstract types
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
		incomplete_ctype, label_ctype, bad_ctype,
		null_ctype;

struct symbol	zero_int;

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

	init_parser(stream);
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
	{ &bool_ctype,	    SYM_BASETYPE, MOD_UNSIGNED,		    &bits_in_bool,	     &max_int_alignment, &int_type },
	{ &void_ctype,	    SYM_BASETYPE, 0,			    NULL,		     NULL,		 NULL },
	{ &type_ctype,	    SYM_BASETYPE, MOD_TYPE,		    NULL,		     NULL,		 NULL },
	{ &incomplete_ctype,SYM_BASETYPE, 0,			    NULL,		     NULL,		 NULL },
	{ &bad_ctype,	    SYM_BASETYPE, 0,			    NULL,		     NULL,		 NULL },

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
	{ &null_ctype,	    SYM_PTR,	  0,			    &bits_in_pointer,        &pointer_alignment, &void_ctype },
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
		unsigned long alignment = (bit_size + 7) >> 3;

		if (alignment > maxalign)
			alignment = maxalign;
		sym->type = ctype->type;
		sym->bit_size = bit_size;
		sym->ctype.alignment = alignment;
		sym->ctype.base_type = ctype->base_type;
		sym->ctype.modifiers = ctype->modifiers;
	}
}
