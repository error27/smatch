/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 * Copyright (C) 2004 Christopher Li
 *
 *  Licensed under the Open Software License version 1.1
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"
#include "target.h"

#define warn_on_mixed (1)

static struct symbol_list **function_symbol_list;
struct symbol_list *function_computed_target_list;
struct statement_list *function_computed_goto_list;

static struct token *statement(struct token *token, struct statement **tree);
static struct token *handle_attributes(struct token *token, struct ctype *ctype, unsigned int keywords);

static struct token *struct_specifier(struct token *token, struct ctype *ctype);
static struct token *union_specifier(struct token *token, struct ctype *ctype);
static struct token *enum_specifier(struct token *token, struct ctype *ctype);
static struct token *attribute_specifier(struct token *token, struct ctype *ctype);
static struct token *typeof_specifier(struct token *token, struct ctype *ctype);

static struct token *parse_if_statement(struct token *token, struct statement *stmt);
static struct token *parse_return_statement(struct token *token, struct statement *stmt);
static struct token *parse_loop_iterator(struct token *token, struct statement *stmt);
static struct token *parse_default_statement(struct token *token, struct statement *stmt);
static struct token *parse_case_statement(struct token *token, struct statement *stmt);
static struct token *parse_switch_statement(struct token *token, struct statement *stmt);
static struct token *parse_for_statement(struct token *token, struct statement *stmt);
static struct token *parse_while_statement(struct token *token, struct statement *stmt);
static struct token *parse_do_statement(struct token *token, struct statement *stmt);
static struct token *parse_goto_statement(struct token *token, struct statement *stmt);
static struct token *parse_context_statement(struct token *token, struct statement *stmt);
static struct token *parse_range_statement(struct token *token, struct statement *stmt);
static struct token *parse_asm_statement(struct token *token, struct statement *stmt);
static struct token *toplevel_asm_declaration(struct token *token, struct symbol_list **list);
static struct token *parse_asm_declarator(struct token *token, struct ctype *ctype);


static struct token *attribute_packed(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_modifier(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_address_space(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_aligned(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_mode(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_context(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *attribute_transparent_union(struct token *token, struct symbol *attr, struct ctype *ctype);
static struct token *ignore_attribute(struct token *token, struct symbol *attr, struct ctype *ctype);


static struct symbol_op modifier_op = {
	.type = KW_MODIFIER,
};

static struct symbol_op qualifier_op = {
	.type = KW_QUALIFIER,
};

static struct symbol_op typeof_op = {
	.type = KW_TYPEOF,
	.declarator = typeof_specifier,
};

static struct symbol_op attribute_op = {
	.type = KW_ATTRIBUTE,
	.declarator = attribute_specifier,
};

static struct symbol_op struct_op = {
	.type = KW_SPECIFIER,
	.declarator = struct_specifier,
};

static struct symbol_op union_op = {
	.type = KW_SPECIFIER,
	.declarator = union_specifier,
};

static struct symbol_op enum_op = {
	.type = KW_SPECIFIER,
	.declarator = enum_specifier,
};



static struct symbol_op if_op = {
	.statement = parse_if_statement,
};

static struct symbol_op return_op = {
	.statement = parse_return_statement,
};

static struct symbol_op loop_iter_op = {
	.statement = parse_loop_iterator,
};

static struct symbol_op default_op = {
	.statement = parse_default_statement,
};

static struct symbol_op case_op = {
	.statement = parse_case_statement,
};

static struct symbol_op switch_op = {
	.statement = parse_switch_statement,
};

static struct symbol_op for_op = {
	.statement = parse_for_statement,
};

static struct symbol_op while_op = {
	.statement = parse_while_statement,
};

static struct symbol_op do_op = {
	.statement = parse_do_statement,
};

static struct symbol_op goto_op = {
	.statement = parse_goto_statement,
};

static struct symbol_op __context___op = {
	.statement = parse_context_statement,
};

static struct symbol_op range_op = {
	.statement = parse_range_statement,
};

static struct symbol_op asm_op = {
	.type = KW_ASM,
	.declarator = parse_asm_declarator,
	.statement = parse_asm_statement,
	.toplevel = toplevel_asm_declaration,
};

static struct symbol_op packed_op = {
	.attribute = attribute_packed,
};

static struct symbol_op aligned_op = {
	.attribute = attribute_aligned,
};

static struct symbol_op attr_mod_op = {
	.attribute = attribute_modifier,
};

static struct symbol_op address_space_op = {
	.attribute = attribute_address_space,
};

static struct symbol_op mode_op = {
	.attribute = attribute_mode,
};

static struct symbol_op context_op = {
	.attribute = attribute_context,
};

static struct symbol_op transparent_union_op = {
	.attribute = attribute_transparent_union,
};

static struct symbol_op ignore_attr_op = {
	.attribute = ignore_attribute,
};

static struct symbol_op mode_spec_op = {
	.type = KW_MODE,
};

static struct init_keyword {
	const char *name;
	enum namespace ns;
	unsigned long modifiers;
	struct symbol_op *op;
} keyword_table[] = {
	/* Type qualifiers */
	{ "const",	NS_TYPEDEF, MOD_CONST, .op = &qualifier_op },
	{ "__const",	NS_TYPEDEF, MOD_CONST, .op = &qualifier_op },
	{ "__const__",	NS_TYPEDEF, MOD_CONST, .op = &qualifier_op },
	{ "volatile",	NS_TYPEDEF, MOD_VOLATILE, .op = &qualifier_op },
	{ "__volatile",		NS_TYPEDEF, MOD_VOLATILE, .op = &qualifier_op },
	{ "__volatile__", 	NS_TYPEDEF, MOD_VOLATILE, .op = &qualifier_op },

	/* Typedef.. */
	{ "typedef",	NS_TYPEDEF, MOD_TYPEDEF, .op = &modifier_op },

	/* Extended types */
	{ "typeof", 	NS_TYPEDEF, .op = &typeof_op },
	{ "__typeof", 	NS_TYPEDEF, .op = &typeof_op },
	{ "__typeof__",	NS_TYPEDEF, .op = &typeof_op },

	{ "__attribute",   NS_TYPEDEF, .op = &attribute_op },
	{ "__attribute__", NS_TYPEDEF, .op = &attribute_op },

	{ "struct",	NS_TYPEDEF, .op = &struct_op },
	{ "union", 	NS_TYPEDEF, .op = &union_op },
	{ "enum", 	NS_TYPEDEF, .op = &enum_op },

	{ "inline",	NS_TYPEDEF, MOD_INLINE, .op = &modifier_op },
	{ "__inline",	NS_TYPEDEF, MOD_INLINE, .op = &modifier_op },
	{ "__inline__",	NS_TYPEDEF, MOD_INLINE, .op = &modifier_op },

	/* Ignored for now.. */
	{ "restrict",	NS_TYPEDEF, .op = &qualifier_op},
	{ "__restrict",	NS_TYPEDEF, .op = &qualifier_op},

	/* Statement */
	{ "if",		NS_KEYWORD, .op = &if_op },
	{ "return",	NS_KEYWORD, .op = &return_op },
	{ "break",	NS_KEYWORD, .op = &loop_iter_op },
	{ "continue",	NS_KEYWORD, .op = &loop_iter_op },
	{ "default",	NS_KEYWORD, .op = &default_op },
	{ "case",	NS_KEYWORD, .op = &case_op },
	{ "switch",	NS_KEYWORD, .op = &switch_op },
	{ "for",	NS_KEYWORD, .op = &for_op },
	{ "while",	NS_KEYWORD, .op = &while_op },
	{ "do",		NS_KEYWORD, .op = &do_op },
	{ "goto",	NS_KEYWORD, .op = &goto_op },
	{ "__context__",NS_KEYWORD, .op = &__context___op },
	{ "__range__",	NS_KEYWORD, .op = &range_op },
	{ "asm",	NS_KEYWORD, .op = &asm_op },
	{ "__asm",	NS_KEYWORD, .op = &asm_op },
	{ "__asm__",	NS_KEYWORD, .op = &asm_op },

	/* Attribute */
	{ "packed",	NS_KEYWORD, .op = &packed_op },
	{ "__packed__",	NS_KEYWORD, .op = &packed_op },
	{ "aligned",	NS_KEYWORD, .op = &aligned_op },
	{ "__aligned__",NS_KEYWORD, .op = &aligned_op },
	{ "nocast",	NS_KEYWORD,	MOD_NOCAST,	.op = &attr_mod_op },
	{ "noderef",	NS_KEYWORD,	MOD_NODEREF,	.op = &attr_mod_op },
	{ "safe",	NS_KEYWORD,	MOD_SAFE, 	.op = &attr_mod_op },
	{ "force",	NS_KEYWORD,	MOD_FORCE,	.op = &attr_mod_op },
	{ "bitwise",	NS_KEYWORD,	MOD_BITWISE,	.op = &attr_mod_op },
	{ "__bitwise__",NS_KEYWORD,	MOD_BITWISE,	.op = &attr_mod_op },
	{ "address_space",NS_KEYWORD,	.op = &address_space_op },
	{ "mode",	NS_KEYWORD,	.op = &mode_op },
	{ "context",	NS_KEYWORD,	.op = &context_op },
	{ "__transparent_union__",	NS_KEYWORD,	.op = &transparent_union_op },

	{ "__mode__",	NS_KEYWORD,	.op = &mode_op },
	{ "QI",		NS_KEYWORD,	MOD_CHAR,	.op = &mode_spec_op },
	{ "__QI__",	NS_KEYWORD,	MOD_CHAR,	.op = &mode_spec_op },
	{ "HI",		NS_KEYWORD,	MOD_SHORT,	.op = &mode_spec_op },
	{ "__HI__",	NS_KEYWORD,	MOD_SHORT,	.op = &mode_spec_op },
	{ "SI",		NS_KEYWORD,			.op = &mode_spec_op },
	{ "__SI__",	NS_KEYWORD,			.op = &mode_spec_op },
	{ "DI",		NS_KEYWORD,	MOD_LONGLONG,	.op = &mode_spec_op },
	{ "__DI__",	NS_KEYWORD,	MOD_LONGLONG,	.op = &mode_spec_op },
	{ "word",	NS_KEYWORD,	MOD_LONG,	.op = &mode_spec_op },
	{ "__word__",	NS_KEYWORD,	MOD_LONG,	.op = &mode_spec_op },

	/* Ignored attributes */
	{ "nothrow",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__nothrow",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__nothrow__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__malloc__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "nonnull",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__nonnull",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__nonnull__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "format",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__format__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "format_arg",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__format_arg__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "section",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__section__",NS_KEYWORD,	.op = &ignore_attr_op },
	{ "unused",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__unused__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "const",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__const",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__const__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "noreturn",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__noreturn__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "no_instrument_function",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__no_instrument_function__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "sentinel",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__sentinel__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "regparm",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__regparm__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "weak",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__weak__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "alias",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__alias__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "pure",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__pure__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "always_inline",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__always_inline__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "syscall_linkage",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__syscall_linkage__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "visibility",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__visibility__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "deprecated",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__deprecated__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "noinline",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__noinline__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "used",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__used__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "warn_unused_result",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__warn_unused_result__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "model",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__model__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "cdecl",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__cdecl__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "stdcall",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__stdcall__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "fastcall",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__fastcall__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "dllimport",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__dllimport__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "dllexport",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__dllexport__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "constructor",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__constructor__",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "destructor",	NS_KEYWORD,	.op = &ignore_attr_op },
	{ "__destructor__",	NS_KEYWORD,	.op = &ignore_attr_op },
};

void init_parser(int stream)
{
	int i;
	for (i = 0; i < sizeof keyword_table/sizeof keyword_table[0]; i++) {
		struct init_keyword *ptr = keyword_table + i;
		struct symbol *sym = create_symbol(stream, ptr->name, SYM_KEYWORD, ptr->ns);
		sym->ident->keyword = 1;
		sym->ctype.modifiers = ptr->modifiers;
		sym->op = ptr->op;
	}
}

// Add a symbol to the list of function-local symbols
static void fn_local_symbol(struct symbol *sym)
{
	if (function_symbol_list)
		add_symbol(function_symbol_list, sym);
}

static int SENTINEL_ATTR match_idents(struct token *token, ...)
{
	va_list args;

	if (token_type(token) != TOKEN_IDENT)
		return 0;

	va_start(args, token);
	for (;;) {
		struct ident * next = va_arg(args, struct ident *);
		if (!next)
			return 0;
		if (token->ident == next)
			return 1;
	}
}


struct statement *alloc_statement(struct position pos, int type)
{
	struct statement *stmt = __alloc_statement(0);
	stmt->type = type;
	stmt->pos = pos;
	return stmt;
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list);

static int apply_modifiers(struct position pos, struct ctype *ctype)
{
	struct symbol *base;

	while ((base = ctype->base_type)) {
		switch (base->type) {
		case SYM_FN:
		case SYM_ENUM:
		case SYM_ARRAY:
		case SYM_BITFIELD:
		case SYM_PTR:
			ctype = &base->ctype;
			continue;
		}
		break;
	}

	/* Turn the "virtual types" into real types with real sizes etc */
	if (ctype->base_type == &int_type) {
		ctype->base_type = ctype_integer(ctype->modifiers);
		ctype->modifiers &= ~MOD_SPECIFIER;
	} else if (ctype->base_type == &fp_type) {
		ctype->base_type = ctype_fp(ctype->modifiers);
		ctype->modifiers &= ~MOD_SPECIFIER;
	}

	if (ctype->modifiers & MOD_BITWISE) {
		struct symbol *type;
		ctype->modifiers &= ~(MOD_BITWISE | MOD_SPECIFIER);
		if (!is_int_type(ctype->base_type)) {
			sparse_error(pos, "invalid modifier");
			return 1;
		}
		type = alloc_symbol(pos, SYM_BASETYPE);
		*type = *ctype->base_type;
		type->ctype.base_type = ctype->base_type;
		type->type = SYM_RESTRICT;
		type->ctype.modifiers &= ~MOD_SPECIFIER;
		ctype->base_type = type;
		create_fouled(type);
	}
	return 0;
}

static struct symbol * alloc_indirect_symbol(struct position pos, struct ctype *ctype, int type)
{
	struct symbol *sym = alloc_symbol(pos, type);

	sym->ctype.base_type = ctype->base_type;
	sym->ctype.modifiers = ctype->modifiers & ~MOD_STORAGE;

	ctype->base_type = sym;
	ctype->modifiers &= MOD_STORAGE;
	return sym;
}

static struct symbol *lookup_or_create_symbol(enum namespace ns, enum type type, struct token *token)
{
	struct symbol *sym = lookup_symbol(token->ident, ns);
	if (!sym) {
		sym = alloc_symbol(token->pos, type);
		bind_symbol(sym, token->ident, ns);
		if (type == SYM_LABEL)
			fn_local_symbol(sym);
	}
	return sym;
}

/*
 * NOTE! NS_LABEL is not just a different namespace,
 * it also ends up using function scope instead of the
 * regular symbol scope.
 */
struct symbol *label_symbol(struct token *token)
{
	return lookup_or_create_symbol(NS_LABEL, SYM_LABEL, token);
}

static struct token *struct_union_enum_specifier(enum type type,
	struct token *token, struct ctype *ctype,
	struct token *(*parse)(struct token *, struct symbol *))
{
	struct symbol *sym;
	struct position *repos;

	ctype->modifiers = 0;
	token = handle_attributes(token, ctype, KW_ATTRIBUTE | KW_ASM);
	if (token_type(token) == TOKEN_IDENT) {
		sym = lookup_symbol(token->ident, NS_STRUCT);
		if (!sym ||
		    (sym->scope != block_scope &&
		     (match_op(token->next,';') || match_op(token->next,'{')))) {
			// Either a new symbol, or else an out-of-scope
			// symbol being redefined.
			sym = alloc_symbol(token->pos, type);
			bind_symbol(sym, token->ident, NS_STRUCT);
		}
		if (sym->type != type)
			error_die(token->pos, "invalid tag applied to %s", show_typename (sym));
		ctype->base_type = sym;
		repos = &token->pos;
		token = token->next;
		if (match_op(token, '{')) {
			// The following test is actually wrong for empty
			// structs, but (1) they are not C99, (2) gcc does
			// the same thing, and (3) it's easier.
			if (sym->symbol_list)
				error_die(token->pos, "redefinition of %s", show_typename (sym));
			sym->pos = *repos;
			token = parse(token->next, sym);
			token = expect(token, '}', "at end of struct-union-enum-specifier");

			// Mark the structure as needing re-examination
			sym->examined = 0;
		}
		return token;
	}

	// private struct/union/enum type
	if (!match_op(token, '{')) {
		sparse_error(token->pos, "expected declaration");
		ctype->base_type = &bad_ctype;
		return token;
	}

	sym = alloc_symbol(token->pos, type);
	token = parse(token->next, sym);
	ctype->base_type = sym;
	return expect(token, '}', "at end of specifier");
}

static struct token *parse_struct_declaration(struct token *token, struct symbol *sym)
{
	struct symbol *field, *last = NULL;
	struct token *res;
	res = struct_declaration_list(token, &sym->symbol_list);
	FOR_EACH_PTR(sym->symbol_list, field) {
		if (!field->ident) {
			struct symbol *base = field->ctype.base_type;
			if (base && base->type == SYM_BITFIELD)
				continue;
		}
		if (last)
			last->next_subobject = field;
		last = field;
	} END_FOR_EACH_PTR(field);
	return res;
}

static struct token *parse_union_declaration(struct token *token, struct symbol *sym)
{
	return struct_declaration_list(token, &sym->symbol_list);
}

static struct token *struct_specifier(struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(SYM_STRUCT, token, ctype, parse_struct_declaration);
}

static struct token *union_specifier(struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(SYM_UNION, token, ctype, parse_union_declaration);
}


typedef struct {
	int x;
	unsigned long long y;
} Num;

static void upper_boundary(Num *n, Num *v)
{
	if (n->x > v->x)
		return;
	if (n->x < v->x) {
		*n = *v;
		return;
	}
	if (n->y < v->y)
		n->y = v->y;
}

static void lower_boundary(Num *n, Num *v)
{
	if (n->x < v->x)
		return;
	if (n->x > v->x) {
		*n = *v;
		return;
	}
	if (n->y > v->y)
		n->y = v->y;
}

static int type_is_ok(struct symbol *type, Num *upper, Num *lower)
{
	int shift = type->bit_size;
	int is_unsigned = type->ctype.modifiers & MOD_UNSIGNED;

	if (!is_unsigned)
		shift--;
	if (upper->x == 0 && upper->y >> shift)
		return 0;
	if (lower->x == 0 || (!is_unsigned && (~lower->y >> shift) == 0))
		return 1;
	return 0;
}

static struct symbol *bigger_enum_type(struct symbol *s1, struct symbol *s2)
{
	if (s1->bit_size < s2->bit_size) {
		s1 = s2;
	} else if (s1->bit_size == s2->bit_size) {
		if (s2->ctype.modifiers & MOD_UNSIGNED)
			s1 = s2;
	}
	if (s1->bit_size < bits_in_int)
		return &int_ctype;
	return s1;
}

static void cast_enum_list(struct symbol_list *list, struct symbol *base_type)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct expression *expr = sym->initializer;
		struct symbol *ctype;
		if (expr->type != EXPR_VALUE)
			continue;
		ctype = expr->ctype;
		if (ctype->bit_size == base_type->bit_size)
			continue;
		cast_value(expr, base_type, expr, ctype);
	} END_FOR_EACH_PTR(sym);
}

static struct token *parse_enum_declaration(struct token *token, struct symbol *parent)
{
	unsigned long long lastval = 0;
	struct symbol *ctype = NULL, *base_type = NULL;
	Num upper = {-1, 0}, lower = {1, 0};
	struct symbol_list *entries = NULL;

	parent->examined = 1;
	parent->ctype.base_type = &int_ctype;
	while (token_type(token) == TOKEN_IDENT) {
		struct expression *expr = NULL;
		struct token *next = token->next;
		struct symbol *sym;

		sym = alloc_symbol(token->pos, SYM_NODE);
		bind_symbol(sym, token->ident, NS_SYMBOL);
		sym->ctype.modifiers &= ~MOD_ADDRESSABLE;

		if (match_op(next, '=')) {
			next = constant_expression(next->next, &expr);
			lastval = get_expression_value(expr);
			ctype = &void_ctype;
			if (expr && expr->ctype)
				ctype = expr->ctype;
		} else if (!ctype) {
			ctype = &int_ctype;
		} else if (is_int_type(ctype)) {
			lastval++;
		} else {
			error_die(token->pos, "can't increment the last enum member");
		}

		if (!expr) {
			expr = alloc_expression(token->pos, EXPR_VALUE);
			expr->value = lastval;
			expr->ctype = ctype;
		}

		sym->initializer = expr;
		sym->enum_member = 1;
		sym->ctype.base_type = parent;
		add_ptr_list(&entries, sym);

		if (base_type != &bad_ctype) {
			if (ctype->type == SYM_NODE)
				ctype = ctype->ctype.base_type;
			if (ctype->type == SYM_ENUM) {
				if (ctype == parent)
					ctype = base_type;
				else 
					ctype = ctype->ctype.base_type;
			}
			/*
			 * base_type rules:
			 *  - if all enums are of the same type, then
			 *    the base_type is that type (two first
			 *    cases)
			 *  - if enums are of different types, they
			 *    all have to be integer types, and the
			 *    base type is at least "int_ctype".
			 *  - otherwise the base_type is "bad_ctype".
			 */
			if (!base_type) {
				base_type = ctype;
			} else if (ctype == base_type) {
				/* nothing */
			} else if (is_int_type(base_type) && is_int_type(ctype)) {
				base_type = bigger_enum_type(base_type, ctype);
			} else
				base_type = &bad_ctype;
			parent->ctype.base_type = base_type;
		}
		if (is_int_type(base_type)) {
			Num v = {.y = lastval};
			if (ctype->ctype.modifiers & MOD_UNSIGNED)
				v.x = 0;
			else if ((long long)lastval >= 0)
				v.x = 0;
			else
				v.x = -1;
			upper_boundary(&upper, &v);
			lower_boundary(&lower, &v);
		}
		token = next;
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	if (!base_type) {
		sparse_error(token->pos, "bad enum definition");
		base_type = &bad_ctype;
	}
	else if (!is_int_type(base_type))
		base_type = base_type;
	else if (type_is_ok(base_type, &upper, &lower))
		base_type = base_type;
	else if (type_is_ok(&int_ctype, &upper, &lower))
		base_type = &int_ctype;
	else if (type_is_ok(&uint_ctype, &upper, &lower))
		base_type = &uint_ctype;
	else if (type_is_ok(&long_ctype, &upper, &lower))
		base_type = &long_ctype;
	else if (type_is_ok(&ulong_ctype, &upper, &lower))
		base_type = &ulong_ctype;
	else if (type_is_ok(&llong_ctype, &upper, &lower))
		base_type = &llong_ctype;
	else if (type_is_ok(&ullong_ctype, &upper, &lower))
		base_type = &ullong_ctype;
	else
		base_type = &bad_ctype;
	parent->ctype.base_type = base_type;
	parent->ctype.modifiers |= (base_type->ctype.modifiers & MOD_UNSIGNED);
	parent->examined = 0;

	cast_enum_list(entries, base_type);
	free_ptr_list(&entries);

	return token;
}

static struct token *enum_specifier(struct token *token, struct ctype *ctype)
{
	struct token *ret = struct_union_enum_specifier(SYM_ENUM, token, ctype, parse_enum_declaration);

	ctype = &ctype->base_type->ctype;
	if (!ctype->base_type)
		ctype->base_type = &incomplete_ctype;

	return ret;
}

static struct token *typeof_specifier(struct token *token, struct ctype *ctype)
{
	struct symbol *sym;

	if (!match_op(token, '(')) {
		sparse_error(token->pos, "expected '(' after typeof");
		return token;
	}
	if (lookup_type(token->next)) {
		token = typename(token->next, &sym);
		*ctype = sym->ctype;
	} else {
		struct symbol *typeof_sym = alloc_symbol(token->pos, SYM_TYPEOF);
		token = parse_expression(token->next, &typeof_sym->initializer);

		ctype->modifiers = 0;
		ctype->base_type = typeof_sym;
	}		
	return expect(token, ')', "after typeof");
}

static struct token *ignore_attribute(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	struct expression *expr = NULL;
	if (match_op(token, '('))
		token = parens_expression(token, &expr, "in attribute");
	return token;
}

static struct token *attribute_packed(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	ctype->alignment = 1;
	return token;
}

static struct token *attribute_aligned(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	int alignment = max_alignment;
	struct expression *expr = NULL;

	if (match_op(token, '(')) {
		token = parens_expression(token, &expr, "in attribute");
		if (expr)
			alignment = get_expression_value(expr);
	}
	ctype->alignment = alignment;
	return token;
}

static struct token *attribute_modifier(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	ctype->modifiers |= attr->ctype.modifiers;
	return token;
}

static struct token *attribute_address_space(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	struct expression *expr = NULL;
	token = expect(token, '(', "after address_space attribute");
	token = conditional_expression(token, &expr);
	if (expr)
		ctype->as = get_expression_value(expr);
	token = expect(token, ')', "after address_space attribute");
	return token;
}

static struct token *attribute_mode(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	token = expect(token, '(', "after mode attribute");
	if (token_type(token) == TOKEN_IDENT) {
		struct symbol *mode = lookup_keyword(token->ident, NS_KEYWORD);
		if (mode && mode->op->type == KW_MODE)
			ctype->modifiers |= mode->ctype.modifiers;
		else
			sparse_error(token->pos, "unknown mode attribute %s\n", show_ident(token->ident));
		token = token->next;
	} else
		sparse_error(token->pos, "expect attribute mode symbol\n");
	token = expect(token, ')', "after mode attribute");
	return token;
}

static struct token *attribute_context(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	struct context *context = alloc_context();
	struct expression *args[3];
	int argc = 0;

	token = expect(token, '(', "after context attribute");
	while (!match_op(token, ')')) {
		struct expression *expr = NULL;
		token = conditional_expression(token, &expr);
		if (!expr)
			break;
		if (argc < 3)
			args[argc++] = expr;
		if (!match_op(token, ','))
			break;
		token = token->next;
	}

	switch(argc) {
	case 0:
		sparse_error(token->pos, "expected context input/output values");
		break;
	case 1:
		context->in = get_expression_value(args[0]);
		break;
	case 2:
		context->in = get_expression_value(args[0]);
		context->out = get_expression_value(args[1]);
		break;
	case 3:
		context->context = args[0];
		context->in = get_expression_value(args[1]);
		context->out = get_expression_value(args[2]);
		break;
	}

	if (argc)
		add_ptr_list(&ctype->contexts, context);

	token = expect(token, ')', "after context attribute");
	return token;
}

static struct token *attribute_transparent_union(struct token *token, struct symbol *attr, struct ctype *ctype)
{
	if (Wtransparent_union)
		sparse_error(token->pos, "ignoring attribute __transparent_union__");
	return token;
}

static struct token *recover_unknown_attribute(struct token *token)
{
	struct expression *expr = NULL;

	sparse_error(token->pos, "attribute '%s': unknown attribute", show_ident(token->ident));
	token = token->next;
	if (match_op(token, '('))
		token = parens_expression(token, &expr, "in attribute");
	return token;
}

static struct token *attribute_specifier(struct token *token, struct ctype *ctype)
{
	ctype->modifiers = 0;
	token = expect(token, '(', "after attribute");
	token = expect(token, '(', "after attribute");

	for (;;) {
		struct ident *attribute_name;
		struct symbol *attr;

		if (eof_token(token))
			break;
		if (match_op(token, ';'))
			break;
		if (token_type(token) != TOKEN_IDENT)
			break;
		attribute_name = token->ident;
		attr = lookup_keyword(attribute_name, NS_KEYWORD);
		if (attr && attr->op->attribute)
			token = attr->op->attribute(token->next, attr, ctype);
		else
			token = recover_unknown_attribute(token);

		if (!match_op(token, ','))
			break;
		token = token->next;
	}

	token = expect(token, ')', "after attribute");
	token = expect(token, ')', "after attribute");
	return token;
}

struct symbol * ctype_integer(unsigned long spec)
{
	static struct symbol *const integer_ctypes[][3] = {
		{ &llong_ctype, &sllong_ctype, &ullong_ctype },
		{ &long_ctype,  &slong_ctype,  &ulong_ctype  },
		{ &short_ctype, &sshort_ctype, &ushort_ctype },
		{ &char_ctype,  &schar_ctype,  &uchar_ctype  },
		{ &int_ctype,   &sint_ctype,   &uint_ctype   },
	};
	struct symbol *const (*ctype)[3];
	int sub;

	ctype = integer_ctypes;
	if (!(spec & MOD_LONGLONG)) {
		ctype++;
		if (!(spec & MOD_LONG)) {
			ctype++;
			if (!(spec & MOD_SHORT)) {
				ctype++;
				if (!(spec & MOD_CHAR))
					ctype++;
			}
		}
	}

	sub = ((spec & MOD_UNSIGNED)
	       ? 2
	       : ((spec & MOD_EXPLICITLY_SIGNED)
		  ? 1
		  : 0));

	return ctype[0][sub];
}

struct symbol * ctype_fp(unsigned long spec)
{
	if (spec & MOD_LONGLONG)
		return &ldouble_ctype;
	if (spec & MOD_LONG)
		return &double_ctype;
	return &float_ctype;
}

static void apply_ctype(struct position pos, struct ctype *thistype, struct ctype *ctype)
{
	unsigned long mod = thistype->modifiers;

	if (mod) {
		unsigned long old = ctype->modifiers;
		unsigned long extra = 0, dup, conflict;

		if (mod & old & MOD_LONG) {
			extra = MOD_LONGLONG | MOD_LONG;
			mod &= ~MOD_LONG;
			old &= ~MOD_LONG;
		}
		dup = (mod & old) | (extra & old) | (extra & mod);
		if (dup)
			sparse_error(pos, "Just how %sdo you want this type to be?",
				modifier_string(dup));

		conflict = !(~mod & ~old & (MOD_LONG | MOD_SHORT));
		if (conflict)
			sparse_error(pos, "You cannot have both long and short modifiers.");

		conflict = !(~mod & ~old & (MOD_SIGNED | MOD_UNSIGNED));
		if (conflict)
			sparse_error(pos, "You cannot have both signed and unsigned modifiers.");

		// Only one storage modifier allowed, except that "inline" doesn't count.
		conflict = (mod | old) & (MOD_STORAGE & ~MOD_INLINE);
		conflict &= (conflict - 1);
		if (conflict)
			sparse_error(pos, "multiple storage classes");

		ctype->modifiers = old | mod | extra;
	}

	/* Context */
	concat_ptr_list((struct ptr_list *)thistype->contexts,
	                (struct ptr_list **)&ctype->contexts);

	/* Alignment */
	if (thistype->alignment & (thistype->alignment-1)) {
		warning(pos, "I don't like non-power-of-2 alignments");
		thistype->alignment = 0;
	}
	if (thistype->alignment > ctype->alignment)
		ctype->alignment = thistype->alignment;

	/* Address space */
	if (thistype->as)
		ctype->as = thistype->as;
}

static void check_modifiers(struct position *pos, struct symbol *s, unsigned long mod)
{
	unsigned long banned, wrong;
	const unsigned long BANNED_SIZE = MOD_LONG | MOD_LONGLONG | MOD_SHORT;
	const unsigned long BANNED_SIGN = MOD_SIGNED | MOD_UNSIGNED;

	if (s->type == SYM_KEYWORD)
		banned = s->op->type == KW_SPECIFIER ? (BANNED_SIZE | BANNED_SIGN) : 0;
	else if (s->ctype.base_type == &fp_type)
		banned = BANNED_SIGN;
	else if (s->ctype.base_type == &int_type || !s->ctype.base_type || is_int_type (s))
		banned = 0;
	else {
		// label_type
		// void_type
		// bad_type
		// vector_type <-- whatever that is
		banned = BANNED_SIZE | BANNED_SIGN;
	}

	wrong = mod & banned;
	if (wrong)
		sparse_error(*pos, "modifier %sis invalid in this context",
		     modifier_string (wrong));
}

static struct token *declaration_specifiers(struct token *next, struct ctype *ctype, int qual)
{
	struct token *token;

	while ( (token = next) != NULL ) {
		struct ctype thistype;
		struct ident *ident;
		struct symbol *s, *type;
		unsigned long mod;

		next = token->next;
		if (token_type(token) != TOKEN_IDENT)
			break;
		ident = token->ident;

		s = lookup_symbol(ident, NS_TYPEDEF);
		if (!s)
			break;
		thistype = s->ctype;
		mod = thistype.modifiers;
		if (qual) {
			if (s->type != SYM_KEYWORD)
				break;
			if (!(s->op->type & (KW_ATTRIBUTE | KW_QUALIFIER)))
				break;
		}
		if (s->type == SYM_KEYWORD && s->op->declarator) {
			next = s->op->declarator(next, &thistype);
			mod = thistype.modifiers;
		}
		type = thistype.base_type;
		if (type) {
			if (qual)
				break;
			if (ctype->base_type)
				break;
			/* User types only mix with qualifiers */
			if (mod & MOD_USERTYPE) {
				if (ctype->modifiers & MOD_SPECIFIER)
					break;
			}
			ctype->base_type = type;
		}

		check_modifiers(&token->pos, s, ctype->modifiers);
		apply_ctype(token->pos, &thistype, ctype);
	}

	if (!ctype->base_type) {
		struct symbol *base = &incomplete_ctype;

		/*
		 * If we have modifiers, we'll default to an integer
		 * type, and "ctype_integer()" will turn this into
		 * a specific one.
		 */
		if (ctype->modifiers & MOD_SPECIFIER)
			base = &int_type;
		ctype->base_type = base;
	}
	return token;
}

static struct token *abstract_array_declarator(struct token *token, struct symbol *sym)
{
	struct expression *expr = NULL;

	token = parse_expression(token, &expr);
	sym->array_size = expr;
	return token;
}

static struct token *parameter_type_list(struct token *, struct symbol *, struct ident **p);
static struct token *declarator(struct token *token, struct symbol *sym, struct ident **p);

static struct token *handle_attributes(struct token *token, struct ctype *ctype, unsigned int keywords)
{
	struct symbol *keyword;
	for (;;) {
		struct ctype thistype = { 0, };
		if (token_type(token) != TOKEN_IDENT)
			break;
		keyword = lookup_keyword(token->ident, NS_KEYWORD | NS_TYPEDEF);
		if (!keyword || keyword->type != SYM_KEYWORD)
			break;
		if (!(keyword->op->type & keywords))
			break;
		token = keyword->op->declarator(token->next, &thistype);
		apply_ctype(token->pos, &thistype, ctype);
	}
	return token;
}

static struct token *direct_declarator(struct token *token, struct symbol *decl, struct ident **p)
{
	struct ctype *ctype = &decl->ctype;

	if (p && token_type(token) == TOKEN_IDENT) {
		*p = token->ident;
		token = token->next;
	}

	for (;;) {
		token = handle_attributes(token, ctype, KW_ATTRIBUTE | KW_ASM);

		if (token_type(token) != TOKEN_SPECIAL)
			return token;

		/*
		 * This can be either a parameter list or a grouping.
		 * For the direct (non-abstract) case, we know if must be
		 * a parameter list if we already saw the identifier.
		 * For the abstract case, we know if must be a parameter
		 * list if it is empty or starts with a type.
		 */
		if (token->special == '(') {
			struct symbol *sym;
			struct token *next = token->next;
			int fn;

			next = handle_attributes(next, ctype, KW_ATTRIBUTE);
			fn = (p && *p) || match_op(next, ')') || lookup_type(next);

			if (!fn) {
				struct symbol *base_type = ctype->base_type;
				token = declarator(next, decl, p);
				token = expect(token, ')', "in nested declarator");
				while (ctype->base_type != base_type)
					ctype = &ctype->base_type->ctype;
				p = NULL;
				continue;
			}

			sym = alloc_indirect_symbol(token->pos, ctype, SYM_FN);
			token = parameter_type_list(next, sym, p);
			token = expect(token, ')', "in function declarator");
			continue;
		}
		if (token->special == '[') {
			struct symbol *array = alloc_indirect_symbol(token->pos, ctype, SYM_ARRAY);
			token = abstract_array_declarator(token->next, array);
			token = expect(token, ']', "in abstract_array_declarator");
			ctype = &array->ctype;
			continue;
		}
		break;
	}
	return token;
}

static struct token *pointer(struct token *token, struct ctype *ctype)
{
	unsigned long modifiers;
	struct symbol *base_type;

	modifiers = ctype->modifiers & ~MOD_TYPEDEF;
	base_type = ctype->base_type;
	ctype->modifiers = modifiers;

	while (match_op(token,'*')) {
		struct symbol *ptr = alloc_symbol(token->pos, SYM_PTR);
		ptr->ctype.modifiers = modifiers & ~MOD_STORAGE;
		ptr->ctype.as = ctype->as;
		concat_ptr_list((struct ptr_list *)ctype->contexts,
				(struct ptr_list **)&ptr->ctype.contexts);
		ptr->ctype.base_type = base_type;

		base_type = ptr;
		ctype->modifiers = modifiers & MOD_STORAGE;
		ctype->base_type = base_type;
		ctype->as = 0;
		free_ptr_list(&ctype->contexts);

		token = declaration_specifiers(token->next, ctype, 1);
		modifiers = ctype->modifiers;
	}
	return token;
}

static struct token *declarator(struct token *token, struct symbol *sym, struct ident **p)
{
	token = pointer(token, &sym->ctype);
	return direct_declarator(token, sym, p);
}

static struct token *handle_bitfield(struct token *token, struct symbol *decl)
{
	struct ctype *ctype = &decl->ctype;
	struct expression *expr;
	struct symbol *bitfield;
	long long width;

	if (ctype->base_type != &int_type && !is_int_type(ctype->base_type)) {
		sparse_error(token->pos, "invalid bitfield specifier for type %s.",
			show_typename(ctype->base_type));
		// Parse this to recover gracefully.
		return conditional_expression(token->next, &expr);
	}

	bitfield = alloc_indirect_symbol(token->pos, ctype, SYM_BITFIELD);
	token = conditional_expression(token->next, &expr);
	width = get_expression_value(expr);
	bitfield->bit_size = width;

	if (width < 0 || width > INT_MAX) {
		sparse_error(token->pos, "invalid bitfield width, %lld.", width);
		width = -1;
	} else if (decl->ident && width == 0) {
		sparse_error(token->pos, "invalid named zero-width bitfield `%s'",
		     show_ident(decl->ident));
		width = -1;
	} else if (decl->ident) {
		struct symbol *base_type = bitfield->ctype.base_type;
		struct symbol *bitfield_type = base_type == &int_type ? bitfield : base_type;
		int is_signed = !(bitfield_type->ctype.modifiers & MOD_UNSIGNED);
		if (Wone_bit_signed_bitfield && width == 1 && is_signed) {
			// Valid values are either {-1;0} or {0}, depending on integer
			// representation.  The latter makes for very efficient code...
			sparse_error(token->pos, "dubious one-bit signed bitfield");
		}
		if (Wdefault_bitfield_sign &&
		    bitfield_type->type != SYM_ENUM &&
		    !(bitfield_type->ctype.modifiers & MOD_EXPLICITLY_SIGNED) &&
		    is_signed) {
			// The sign of bitfields is unspecified by default.
			sparse_error(token->pos, "dubious bitfield without explicit `signed' or `unsigned'");
		}
	}
	bitfield->bit_size = width;
	return token;
}

static struct token *declaration_list(struct token *token, struct symbol_list **list)
{
	struct ctype ctype = {0, };

	token = declaration_specifiers(token, &ctype, 0);
	for (;;) {
		struct ident *ident = NULL;
		struct symbol *decl = alloc_symbol(token->pos, SYM_NODE);
		decl->ctype = ctype;
		token = declarator(token, decl, &ident);
		decl->ident = ident;
		if (match_op(token, ':')) {
			token = handle_bitfield(token, decl);
			token = handle_attributes(token, &decl->ctype, KW_ATTRIBUTE | KW_ASM);
		}
		apply_modifiers(token->pos, &decl->ctype);
		add_symbol(list, decl);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	return token;
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list)
{
	while (!match_op(token, '}')) {
		if (!match_op(token, ';'))
			token = declaration_list(token, list);
		if (!match_op(token, ';')) {
			sparse_error(token->pos, "expected ; at end of declaration");
			break;
		}
		token = token->next;
	}
	return token;
}

static struct token *parameter_declaration(struct token *token, struct symbol **tree)
{
	struct ident *ident = NULL;
	struct symbol *sym;
	struct ctype ctype = { 0, };

	token = declaration_specifiers(token, &ctype, 0);
	sym = alloc_symbol(token->pos, SYM_NODE);
	sym->ctype = ctype;
	*tree = sym;
	token = declarator(token, sym, &ident);
	sym->ident = ident;
	apply_modifiers(token->pos, &sym->ctype);
	return token;
}

struct token *typename(struct token *token, struct symbol **p)
{
	struct symbol *sym = alloc_symbol(token->pos, SYM_NODE);
	*p = sym;
	token = declaration_specifiers(token, &sym->ctype, 0);
	token = declarator(token, sym, NULL);
	apply_modifiers(token->pos, &sym->ctype);
	return token;
}

static struct token *expression_statement(struct token *token, struct expression **tree)
{
	token = parse_expression(token, tree);
	return expect(token, ';', "at end of statement");
}

static struct token *parse_asm_operands(struct token *token, struct statement *stmt,
	struct expression_list **inout)
{
	struct expression *expr;

	/* Allow empty operands */
	if (match_op(token->next, ':') || match_op(token->next, ')'))
		return token->next;
	do {
		struct ident *ident = NULL;
		if (match_op(token->next, '[') &&
		    token_type(token->next->next) == TOKEN_IDENT &&
		    match_op(token->next->next->next, ']')) {
			ident = token->next->next->ident;
			token = token->next->next->next;
		}
		add_expression(inout, (struct expression *)ident); /* UGGLEE!!! */
		token = primary_expression(token->next, &expr);
		add_expression(inout, expr);
		token = parens_expression(token, &expr, "in asm parameter");
		add_expression(inout, expr);
	} while (match_op(token, ','));
	return token;
}

static struct token *parse_asm_clobbers(struct token *token, struct statement *stmt,
	struct expression_list **clobbers)
{
	struct expression *expr;

	do {
		token = primary_expression(token->next, &expr);
		add_expression(clobbers, expr);
	} while (match_op(token, ','));
	return token;
}

static struct token *parse_asm_statement(struct token *token, struct statement *stmt)
{
	token = token->next;
	stmt->type = STMT_ASM;
	if (match_idents(token, &__volatile___ident, &__volatile_ident, &volatile_ident, NULL)) {
		token = token->next;
	}
	token = expect(token, '(', "after asm");
	token = parse_expression(token, &stmt->asm_string);
	if (match_op(token, ':'))
		token = parse_asm_operands(token, stmt, &stmt->asm_outputs);
	if (match_op(token, ':'))
		token = parse_asm_operands(token, stmt, &stmt->asm_inputs);
	if (match_op(token, ':'))
		token = parse_asm_clobbers(token, stmt, &stmt->asm_clobbers);
	token = expect(token, ')', "after asm");
	return expect(token, ';', "at end of asm-statement");
}

static struct token *parse_asm_declarator(struct token *token, struct ctype *ctype)
{
	struct expression *expr;
	token = expect(token, '(', "after asm");
	token = parse_expression(token->next, &expr);
	token = expect(token, ')', "after asm");
	return token;
}

/* Make a statement out of an expression */
static struct statement *make_statement(struct expression *expr)
{
	struct statement *stmt;

	if (!expr)
		return NULL;
	stmt = alloc_statement(expr->pos, STMT_EXPRESSION);
	stmt->expression = expr;
	return stmt;
}

/*
 * All iterators have two symbols associated with them:
 * the "continue" and "break" symbols, which are targets
 * for continue and break statements respectively.
 *
 * They are in a special name-space, but they follow
 * all the normal visibility rules, so nested iterators
 * automatically work right.
 */
static void start_iterator(struct statement *stmt)
{
	struct symbol *cont, *brk;

	start_symbol_scope();
	cont = alloc_symbol(stmt->pos, SYM_NODE);
	bind_symbol(cont, &continue_ident, NS_ITERATOR);
	brk = alloc_symbol(stmt->pos, SYM_NODE);
	bind_symbol(brk, &break_ident, NS_ITERATOR);

	stmt->type = STMT_ITERATOR;
	stmt->iterator_break = brk;
	stmt->iterator_continue = cont;
	fn_local_symbol(brk);
	fn_local_symbol(cont);
}

static void end_iterator(struct statement *stmt)
{
	end_symbol_scope();
}

static struct statement *start_function(struct symbol *sym)
{
	struct symbol *ret;
	struct statement *stmt = alloc_statement(sym->pos, STMT_COMPOUND);

	start_function_scope();
	ret = alloc_symbol(sym->pos, SYM_NODE);
	ret->ctype = sym->ctype.base_type->ctype;
	ret->ctype.modifiers &= ~(MOD_STORAGE | MOD_CONST | MOD_VOLATILE | MOD_INLINE | MOD_ADDRESSABLE | MOD_NOCAST | MOD_NODEREF | MOD_ACCESSED | MOD_TOPLEVEL);
	ret->ctype.modifiers |= (MOD_AUTO | MOD_REGISTER);
	bind_symbol(ret, &return_ident, NS_ITERATOR);
	stmt->ret = ret;
	fn_local_symbol(ret);

	// Currently parsed symbol for __func__/__FUNCTION__/__PRETTY_FUNCTION__
	current_fn = sym;

	return stmt;
}

static void end_function(struct symbol *sym)
{
	current_fn = NULL;
	end_function_scope();
}

/*
 * A "switch()" statement, like an iterator, has a
 * the "break" symbol associated with it. It works
 * exactly like the iterator break - it's the target
 * for any break-statements in scope, and means that
 * "break" handling doesn't even need to know whether
 * it's breaking out of an iterator or a switch.
 *
 * In addition, the "case" symbol is a marker for the
 * case/default statements to find the switch statement
 * that they are associated with.
 */
static void start_switch(struct statement *stmt)
{
	struct symbol *brk, *switch_case;

	start_symbol_scope();
	brk = alloc_symbol(stmt->pos, SYM_NODE);
	bind_symbol(brk, &break_ident, NS_ITERATOR);

	switch_case = alloc_symbol(stmt->pos, SYM_NODE);
	bind_symbol(switch_case, &case_ident, NS_ITERATOR);
	switch_case->stmt = stmt;

	stmt->type = STMT_SWITCH;
	stmt->switch_break = brk;
	stmt->switch_case = switch_case;

	fn_local_symbol(brk);
	fn_local_symbol(switch_case);
}

static void end_switch(struct statement *stmt)
{
	if (!stmt->switch_case->symbol_list)
		warning(stmt->pos, "switch with no cases");
	end_symbol_scope();
}

static void add_case_statement(struct statement *stmt)
{
	struct symbol *target = lookup_symbol(&case_ident, NS_ITERATOR);
	struct symbol *sym;

	if (!target) {
		sparse_error(stmt->pos, "not in switch scope");
		stmt->type = STMT_NONE;
		return;
	}
	sym = alloc_symbol(stmt->pos, SYM_NODE);
	add_symbol(&target->symbol_list, sym);
	sym->stmt = stmt;
	stmt->case_label = sym;
	fn_local_symbol(sym);
}

static struct token *parse_return_statement(struct token *token, struct statement *stmt)
{
	struct symbol *target = lookup_symbol(&return_ident, NS_ITERATOR);

	if (!target)
		error_die(token->pos, "internal error: return without a function target");
	stmt->type = STMT_RETURN;
	stmt->ret_target = target;
	return expression_statement(token->next, &stmt->ret_value);
}

static struct token *parse_for_statement(struct token *token, struct statement *stmt)
{
	struct symbol_list *syms;
	struct expression *e1, *e2, *e3;
	struct statement *iterator;

	start_iterator(stmt);
	token = expect(token->next, '(', "after 'for'");

	syms = NULL;
	e1 = NULL;
	/* C99 variable declaration? */
	if (lookup_type(token)) {
		token = external_declaration(token, &syms);
	} else {
		token = parse_expression(token, &e1);
		token = expect(token, ';', "in 'for'");
	}
	token = parse_expression(token, &e2);
	token = expect(token, ';', "in 'for'");
	token = parse_expression(token, &e3);
	token = expect(token, ')', "in 'for'");
	token = statement(token, &iterator);

	stmt->iterator_syms = syms;
	stmt->iterator_pre_statement = make_statement(e1);
	stmt->iterator_pre_condition = e2;
	stmt->iterator_post_statement = make_statement(e3);
	stmt->iterator_post_condition = NULL;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	return token;
}

static struct token *parse_while_statement(struct token *token, struct statement *stmt)
{
	struct expression *expr;
	struct statement *iterator;

	start_iterator(stmt);
	token = parens_expression(token->next, &expr, "after 'while'");
	token = statement(token, &iterator);

	stmt->iterator_pre_condition = expr;
	stmt->iterator_post_condition = NULL;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	return token;
}

static struct token *parse_do_statement(struct token *token, struct statement *stmt)
{
	struct expression *expr;
	struct statement *iterator;

	start_iterator(stmt);
	token = statement(token->next, &iterator);
	if (token_type(token) == TOKEN_IDENT && token->ident == &while_ident)
		token = token->next;
	else
		sparse_error(token->pos, "expected 'while' after 'do'");
	token = parens_expression(token, &expr, "after 'do-while'");

	stmt->iterator_post_condition = expr;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	if (iterator && iterator->type != STMT_COMPOUND && Wdo_while)
		warning(iterator->pos, "do-while statement is not a compound statement");

	return expect(token, ';', "after statement");
}

static struct token *parse_if_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_IF;
	token = parens_expression(token->next, &stmt->if_conditional, "after if");
	token = statement(token, &stmt->if_true);
	if (token_type(token) != TOKEN_IDENT)
		return token;
	if (token->ident != &else_ident)
		return token;
	return statement(token->next, &stmt->if_false);
}

static inline struct token *case_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_CASE;
	token = expect(token, ':', "after default/case");
	add_case_statement(stmt);
	return statement(token, &stmt->case_statement);
}

static struct token *parse_case_statement(struct token *token, struct statement *stmt)
{
	token = parse_expression(token->next, &stmt->case_expression);
	if (match_op(token, SPECIAL_ELLIPSIS))
		token = parse_expression(token->next, &stmt->case_to);
	return case_statement(token, stmt);
}

static struct token *parse_default_statement(struct token *token, struct statement *stmt)
{
	return case_statement(token->next, stmt);
}

static struct token *parse_loop_iterator(struct token *token, struct statement *stmt)
{
	struct symbol *target = lookup_symbol(token->ident, NS_ITERATOR);
	stmt->type = STMT_GOTO;
	stmt->goto_label = target;
	if (!target)
		sparse_error(stmt->pos, "break/continue not in iterator scope");
	return expect(token->next, ';', "at end of statement");
}

static struct token *parse_switch_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_SWITCH;
	start_switch(stmt);
	token = parens_expression(token->next, &stmt->switch_expression, "after 'switch'");
	token = statement(token, &stmt->switch_statement);
	end_switch(stmt);
	return token;
}

static struct token *parse_goto_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_GOTO;
	token = token->next;
	if (match_op(token, '*')) {
		token = parse_expression(token->next, &stmt->goto_expression);
		add_statement(&function_computed_goto_list, stmt);
	} else if (token_type(token) == TOKEN_IDENT) {
		stmt->goto_label = label_symbol(token);
		token = token->next;
	} else {
		sparse_error(token->pos, "Expected identifier or goto expression");
	}
	return expect(token, ';', "at end of statement");
}

static struct token *parse_context_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_CONTEXT;
	token = parse_expression(token->next, &stmt->expression);
	if(stmt->expression->type == EXPR_PREOP
	   && stmt->expression->op == '('
	   && stmt->expression->unop->type == EXPR_COMMA) {
		struct expression *expr;
		expr = stmt->expression->unop;
		stmt->context = expr->left;
		stmt->expression = expr->right;
	}
	return expect(token, ';', "at end of statement");
}

static struct token *parse_range_statement(struct token *token, struct statement *stmt)
{
	stmt->type = STMT_RANGE;
	token = assignment_expression(token->next, &stmt->range_expression);
	token = expect(token, ',', "after range expression");
	token = assignment_expression(token, &stmt->range_low);
	token = expect(token, ',', "after low range");
	token = assignment_expression(token, &stmt->range_high);
	return expect(token, ';', "after range statement");
}

static struct token *statement(struct token *token, struct statement **tree)
{
	struct statement *stmt = alloc_statement(token->pos, STMT_NONE);

	*tree = stmt;
	if (token_type(token) == TOKEN_IDENT) {
		struct symbol *s = lookup_keyword(token->ident, NS_KEYWORD);
		if (s && s->op->statement)
			return s->op->statement(token, stmt);

		if (match_op(token->next, ':')) {
			stmt->type = STMT_LABEL;
			stmt->label_identifier = label_symbol(token);
			token = handle_attributes(token->next->next, &stmt->label_identifier->ctype, KW_ATTRIBUTE);
			return statement(token, &stmt->label_statement);
		}
	}

	if (match_op(token, '{')) {
		stmt->type = STMT_COMPOUND;
		start_symbol_scope();
		token = compound_statement(token->next, stmt);
		end_symbol_scope();
		
		return expect(token, '}', "at end of compound statement");
	}
			
	stmt->type = STMT_EXPRESSION;
	return expression_statement(token, &stmt->expression);
}

static struct token * statement_list(struct token *token, struct statement_list **list)
{
	int seen_statement = 0;
	for (;;) {
		struct statement * stmt;
		if (eof_token(token))
			break;
		if (match_op(token, '}'))
			break;
		if (lookup_type(token)) {
			if (seen_statement) {
				warning(token->pos, "mixing declarations and code");
				seen_statement = 0;
			}
			stmt = alloc_statement(token->pos, STMT_DECLARATION);
			token = external_declaration(token, &stmt->declaration);
		} else {
			seen_statement = warn_on_mixed;
			token = statement(token, &stmt);
		}
		add_statement(list, stmt);
	}
	return token;
}

static struct token *parameter_type_list(struct token *token, struct symbol *fn, struct ident **p)
{
	struct symbol_list **list = &fn->arguments;

	if (match_op(token, ')')) {
		// No warning for "void oink ();"
		// Bug or feature: warns for "void oink () __attribute__ ((noreturn));"
		if (p && !match_op(token->next, ';'))
			warning(token->pos, "non-ANSI function declaration of function '%s'", show_ident(*p));
		return token;
	}

	for (;;) {
		struct symbol *sym;

		if (match_op(token, SPECIAL_ELLIPSIS)) {
			if (!*list)
				warning(token->pos, "variadic functions must have one named argument");
			fn->variadic = 1;
			token = token->next;
			break;
		}

		sym = alloc_symbol(token->pos, SYM_NODE);
		token = parameter_declaration(token, &sym);
		if (sym->ctype.base_type == &void_ctype) {
			/* Special case: (void) */
			if (!*list && !sym->ident)
				break;
			warning(token->pos, "void parameter");
		}
		add_symbol(list, sym);
		if (!match_op(token, ','))
			break;
		token = token->next;

	}
	return token;
}

struct token *compound_statement(struct token *token, struct statement *stmt)
{
	token = statement_list(token, &stmt->stmts);
	return token;
}

static struct expression *identifier_expression(struct token *token)
{
	struct expression *expr = alloc_expression(token->pos, EXPR_IDENTIFIER);
	expr->expr_ident = token->ident;
	return expr;
}

static struct expression *index_expression(struct expression *from, struct expression *to)
{
	int idx_from, idx_to;
	struct expression *expr = alloc_expression(from->pos, EXPR_INDEX);

	idx_from = get_expression_value(from);
	idx_to = idx_from;
	if (to) {
		idx_to = get_expression_value(to);
		if (idx_to < idx_from || idx_from < 0)
			warning(from->pos, "nonsense array initializer index range");
	}
	expr->idx_from = idx_from;
	expr->idx_to = idx_to;
	return expr;
}

static struct token *single_initializer(struct expression **ep, struct token *token)
{
	int expect_equal = 0;
	struct token *next = token->next;
	struct expression **tail = ep;
	int nested;

	*ep = NULL; 

	if ((token_type(token) == TOKEN_IDENT) && match_op(next, ':')) {
		struct expression *expr = identifier_expression(token);
		if (Wold_initializer)
			warning(token->pos, "obsolete struct initializer, use C99 syntax");
		token = initializer(&expr->ident_expression, next->next);
		if (expr->ident_expression)
			*ep = expr;
		return token;
	}

	for (tail = ep, nested = 0; ; nested++, next = token->next) {
		if (match_op(token, '.') && (token_type(next) == TOKEN_IDENT)) {
			struct expression *expr = identifier_expression(next);
			*tail = expr;
			tail = &expr->ident_expression;
			expect_equal = 1;
			token = next->next;
		} else if (match_op(token, '[')) {
			struct expression *from = NULL, *to = NULL, *expr;
			token = constant_expression(token->next, &from);
			if (!from) {
				sparse_error(token->pos, "Expected constant expression");
				break;
			}
			if (match_op(token, SPECIAL_ELLIPSIS))
				token = constant_expression(token->next, &to);
			expr = index_expression(from, to);
			*tail = expr;
			tail = &expr->idx_expression;
			token = expect(token, ']', "at end of initializer index");
			if (nested)
				expect_equal = 1;
		} else {
			break;
		}
	}
	if (nested && !expect_equal) {
		if (!match_op(token, '='))
			warning(token->pos, "obsolete array initializer, use C99 syntax");
		else
			expect_equal = 1;
	}
	if (expect_equal)
		token = expect(token, '=', "at end of initializer index");

	token = initializer(tail, token);
	if (!*tail)
		*ep = NULL;
	return token;
}

static struct token *initializer_list(struct expression_list **list, struct token *token)
{
	struct expression *expr;

	for (;;) {
		token = single_initializer(&expr, token);
		if (!expr)
			break;
		add_expression(list, expr);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	return token;
}

struct token *initializer(struct expression **tree, struct token *token)
{
	if (match_op(token, '{')) {
		struct expression *expr = alloc_expression(token->pos, EXPR_INITIALIZER);
		*tree = expr;
		token = initializer_list(&expr->expr_list, token->next);
		return expect(token, '}', "at end of initializer");
	}
	return assignment_expression(token, tree);
}

static void declare_argument(struct symbol *sym, struct symbol *fn)
{
	if (!sym->ident) {
		sparse_error(sym->pos, "no identifier for function argument");
		return;
	}
	bind_symbol(sym, sym->ident, NS_SYMBOL);
}

static struct token *parse_function_body(struct token *token, struct symbol *decl,
	struct symbol_list **list)
{
	struct symbol_list **old_symbol_list;
	struct symbol *base_type = decl->ctype.base_type;
	struct statement *stmt, **p;
	struct symbol *arg;

	old_symbol_list = function_symbol_list;
	if (decl->ctype.modifiers & MOD_INLINE) {
		function_symbol_list = &decl->inline_symbol_list;
		p = &base_type->inline_stmt;
	} else {
		function_symbol_list = &decl->symbol_list;
		p = &base_type->stmt;
	}
	function_computed_target_list = NULL;
	function_computed_goto_list = NULL;

	if (decl->ctype.modifiers & MOD_EXTERN) {
		if (!(decl->ctype.modifiers & MOD_INLINE))
			warning(decl->pos, "function '%s' with external linkage has definition", show_ident(decl->ident));
	}
	if (!(decl->ctype.modifiers & MOD_STATIC))
		decl->ctype.modifiers |= MOD_EXTERN;

	stmt = start_function(decl);

	*p = stmt;
	FOR_EACH_PTR (base_type->arguments, arg) {
		declare_argument(arg, base_type);
	} END_FOR_EACH_PTR(arg);

	token = compound_statement(token->next, stmt);

	end_function(decl);
	if (!(decl->ctype.modifiers & MOD_INLINE))
		add_symbol(list, decl);
	check_declaration(decl);
	function_symbol_list = old_symbol_list;
	if (function_computed_goto_list) {
		if (!function_computed_target_list)
			warning(decl->pos, "function '%s' has computed goto but no targets?", show_ident(decl->ident));
		else {
			FOR_EACH_PTR(function_computed_goto_list, stmt) {
				stmt->target_list = function_computed_target_list;
			} END_FOR_EACH_PTR(stmt);
		}
	}
	return expect(token, '}', "at end of function");
}

static void promote_k_r_types(struct symbol *arg)
{
	struct symbol *base = arg->ctype.base_type;
	if (base && base->ctype.base_type == &int_type && (base->ctype.modifiers & (MOD_CHAR | MOD_SHORT))) {
		arg->ctype.base_type = &int_ctype;
	}
}

static void apply_k_r_types(struct symbol_list *argtypes, struct symbol *fn)
{
	struct symbol_list *real_args = fn->ctype.base_type->arguments;
	struct symbol *arg;

	FOR_EACH_PTR(real_args, arg) {
		struct symbol *type;

		/* This is quadratic in the number of arguments. We _really_ don't care */
		FOR_EACH_PTR(argtypes, type) {
			if (type->ident == arg->ident)
				goto match;
		} END_FOR_EACH_PTR(type);
		sparse_error(arg->pos, "missing type declaration for parameter '%s'", show_ident(arg->ident));
		continue;
match:
		type->used = 1;
		/* "char" and "short" promote to "int" */
		promote_k_r_types(type);

		arg->ctype = type->ctype;
	} END_FOR_EACH_PTR(arg);

	FOR_EACH_PTR(argtypes, arg) {
		if (!arg->used)
			warning(arg->pos, "nonsensical parameter declaration '%s'", show_ident(arg->ident));
	} END_FOR_EACH_PTR(arg);

}

static struct token *parse_k_r_arguments(struct token *token, struct symbol *decl,
	struct symbol_list **list)
{
	struct symbol_list *args = NULL;

	warning(token->pos, "non-ANSI definition of function '%s'", show_ident(decl->ident));
	do {
		token = declaration_list(token, &args);
		if (!match_op(token, ';')) {
			sparse_error(token->pos, "expected ';' at end of parameter declaration");
			break;
		}
		token = token->next;
	} while (lookup_type(token));

	apply_k_r_types(args, decl);

	if (!match_op(token, '{')) {
		sparse_error(token->pos, "expected function body");
		return token;
	}
	return parse_function_body(token, decl, list);
}

static struct token *toplevel_asm_declaration(struct token *token, struct symbol_list **list)
{
	struct symbol *anon = alloc_symbol(token->pos, SYM_NODE);
	struct symbol *fn = alloc_symbol(token->pos, SYM_FN);
	struct statement *stmt;

	anon->ctype.base_type = fn;
	stmt = alloc_statement(token->pos, STMT_NONE);
	fn->stmt = stmt;

	token = parse_asm_statement(token, stmt);

	add_symbol(list, anon);
	return token;
}

struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct ident *ident = NULL;
	struct symbol *decl;
	struct ctype ctype = { 0, };
	struct symbol *base_type;
	int is_typedef;

	/* Top-level inline asm? */
	if (token_type(token) == TOKEN_IDENT) {
		struct symbol *s = lookup_keyword(token->ident, NS_KEYWORD);
		if (s && s->op->toplevel)
			return s->op->toplevel(token, list);
	}

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype, 0);
	decl = alloc_symbol(token->pos, SYM_NODE);
	decl->ctype = ctype;
	token = declarator(token, decl, &ident);
	apply_modifiers(token->pos, &decl->ctype);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	/* type define declaration? */
	is_typedef = (ctype.modifiers & MOD_TYPEDEF) != 0;

	/* Typedefs don't have meaningful storage */
	if (is_typedef) {
		ctype.modifiers &= ~MOD_STORAGE;
		decl->ctype.modifiers &= ~MOD_STORAGE;
		decl->ctype.modifiers |= MOD_USERTYPE;
	}

	bind_symbol(decl, ident, is_typedef ? NS_TYPEDEF: NS_SYMBOL);

	base_type = decl->ctype.base_type;

	if (is_typedef) {
		if (base_type && !base_type->ident)
			base_type->ident = ident;
	} else if (base_type && base_type->type == SYM_FN) {
		/* K&R argument declaration? */
		if (lookup_type(token))
			return parse_k_r_arguments(token, decl, list);
		if (match_op(token, '{'))
			return parse_function_body(token, decl, list);

		if (!(decl->ctype.modifiers & MOD_STATIC))
			decl->ctype.modifiers |= MOD_EXTERN;
	} else if (base_type == &void_ctype && !(decl->ctype.modifiers & MOD_EXTERN)) {
		sparse_error(token->pos, "void declaration");
	}

	for (;;) {
		if (!is_typedef && match_op(token, '=')) {
			if (decl->ctype.modifiers & MOD_EXTERN) {
				warning(decl->pos, "symbol with external linkage has initializer");
				decl->ctype.modifiers &= ~MOD_EXTERN;
			}
			token = initializer(&decl->initializer, token->next);
		}
		if (!is_typedef) {
			if (!(decl->ctype.modifiers & (MOD_EXTERN | MOD_INLINE))) {
				add_symbol(list, decl);
				fn_local_symbol(decl);
			}
		}
		check_declaration(decl);

		if (!match_op(token, ','))
			break;

		token = token->next;
		ident = NULL;
		decl = alloc_symbol(token->pos, SYM_NODE);
		decl->ctype = ctype;
		token = declaration_specifiers(token, &decl->ctype, 1);
		token = declarator(token, decl, &ident);
		apply_modifiers(token->pos, &decl->ctype);
		if (!ident) {
			sparse_error(token->pos, "expected identifier name in type definition");
			return token;
		}

		bind_symbol(decl, ident, is_typedef ? NS_TYPEDEF: NS_SYMBOL);

		/* Function declarations are automatically extern unless specifically static */
		base_type = decl->ctype.base_type;
		if (!is_typedef && base_type && base_type->type == SYM_FN) {
			if (!(decl->ctype.modifiers & MOD_STATIC))
				decl->ctype.modifiers |= MOD_EXTERN;
		}
	}
	return expect(token, ';', "at end of declaration");
}
