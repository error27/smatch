/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
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
static struct token *external_declaration(struct token *token, struct symbol_list **list);

// Add a symbol to the list of function-local symbols
static void fn_local_symbol(struct symbol *sym)
{
	if (function_symbol_list)
		add_symbol(function_symbol_list, sym);
}

static int match_idents(struct token *token, ...)
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

static struct symbol * indirect(struct position pos, struct ctype *ctype, int type)
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

struct token *struct_union_enum_specifier(enum type type,
	struct token *token, struct ctype *ctype,
	struct token *(*parse)(struct token *, struct symbol *))
{
	struct symbol *sym;

	ctype->modifiers = 0;
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
		token = token->next;
		ctype->base_type = sym;
		if (match_op(token, '{')) {
			// The following test is actually wrong for empty
			// structs, but (1) they are not C99, (2) gcc does
			// the same thing, and (3) it's easier.
			if (sym->symbol_list)
				error_die(token->pos, "redefinition of %s", show_typename (sym));
			token = parse(token->next, sym);
			token = expect(token, '}', "at end of struct-union-enum-specifier");
		}
		return token;
	}

	// private struct/union/enum type
	if (!match_op(token, '{')) {
		warning(token->pos, "expected declaration");
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
	return struct_declaration_list(token, &sym->symbol_list);
}

struct token *struct_or_union_specifier(enum type type, struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(type, token, ctype, parse_struct_declaration);
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

static struct token *parse_enum_declaration(struct token *token, struct symbol *parent)
{
	unsigned long long lastval = 0;
	struct symbol *ctype = NULL, *base_type = NULL;
	Num upper = {-1, 0}, lower = {1, 0};

	while (token_type(token) == TOKEN_IDENT) {
		struct token *next = token->next;
		struct symbol *sym;

		sym = alloc_symbol(token->pos, SYM_ENUM);
		bind_symbol(sym, token->ident, NS_SYMBOL);

		if (match_op(next, '=')) {
			struct expression *expr;
			next = constant_expression(next->next, &expr);
			lastval = get_expression_value(expr);
			ctype = expr->ctype;
		} else if (!ctype) {
			ctype = &int_ctype;
		} else if (is_int_type(ctype)) {
			lastval++;
		} else {
			error_die(token->pos, "can't increment the last enum member");
		}

		sym->value = lastval;
		sym->ctype.base_type = ctype;

		if (base_type != &bad_ctype) {
			if (ctype->type == SYM_NODE)
				ctype = ctype->ctype.base_type;
			if (ctype->type == SYM_ENUM)
				ctype = ctype->ctype.base_type;
			/*
			 * base_type rules:
			 *  - if all enum's are of the same type, then
			 *    the base_type is that type (two first
			 *    cases)
			 *  - if enums are of different types, they
			 *    all have to be integer types, and the
			 *    base type is "int_ctype".
			 *  - otherwise the base_type is "bad_ctype".
			 */
			if (!base_type) {
				base_type = ctype;
			} else if (ctype == base_type) {
				/* nothing */
			} else if (is_int_type(base_type) && is_int_type(ctype)) {
				base_type = &int_ctype;
			} else
				base_type = &bad_ctype;
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
	if (!base_type)
		base_type = &bad_ctype;
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
	return token;
}

struct token *enum_specifier(struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(SYM_ENUM, token, ctype, parse_enum_declaration);
}

struct token *typeof_specifier(struct token *token, struct ctype *ctype)
{
	struct symbol *sym;

	if (!match_op(token, '(')) {
		warning(token->pos, "expected '(' after typeof");
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

static const char * handle_attribute(struct ctype *ctype, struct ident *attribute, struct expression *expr)
{
	if (attribute == &packed_ident ||
	    attribute == &__packed___ident) {
		ctype->alignment = 1;
		return NULL;
	}
	if (attribute == &aligned_ident ||
	    attribute == &__aligned___ident) {
		int alignment = max_alignment;
		if (expr)
			alignment = get_expression_value(expr);
		ctype->alignment = alignment;
		return NULL;
	}
	if (attribute == &nocast_ident) {
		ctype->modifiers |= MOD_NOCAST;
		return NULL;
	}
	if (attribute == &noderef_ident) {
		ctype->modifiers |= MOD_NODEREF;
		return NULL;
	}
	if (attribute == &safe_ident) {
		ctype->modifiers |= MOD_SAFE;
		return NULL;
	}
	if (attribute == &force_ident) {
		ctype->modifiers |= MOD_FORCE;
		return NULL;
	}
	if (attribute == &bitwise_ident) {
		if (Wbitwise)
			ctype->modifiers |= MOD_BITWISE;
		return NULL;
	}
	if (attribute == &address_space_ident) {
		if (!expr)
			return "expected address space number";
		ctype->as = get_expression_value(expr);
		return NULL;
	}
	if (attribute == &context_ident) {
		if (expr && expr->type == EXPR_COMMA) {
			int input = get_expression_value(expr->left);
			int output = get_expression_value(expr->right);
			ctype->in_context = input;
			ctype->out_context = output;
			return NULL;
		}
		return "expected context input/output values";
	}
	if (attribute == &mode_ident ||
	    attribute == &__mode___ident) {
		if (expr && expr->type == EXPR_SYMBOL) {
			struct ident *ident = expr->symbol_name;

			/*
			 * Match against __QI__/__HI__/__SI__/__DI__
			 *
			 * FIXME! This is broken - we don't actually get
			 * the type information updated properly at this
			 * stage for some reason.
			 */
			if (ident == &__QI___ident ||
			    ident == &QI_ident) {
				ctype->modifiers |= MOD_CHAR;
				return NULL;
			}
			if (ident == &__HI___ident ||
			    ident == &HI_ident) {
				ctype->modifiers |= MOD_SHORT;
				return NULL;
			}
			if (ident == &__SI___ident ||
			    ident == &SI_ident) {
				/* Nothing? */
				return NULL;
			}
			if (ident == &__DI___ident ||
			    ident == &DI_ident) {
				ctype->modifiers |= MOD_LONGLONG;
				return NULL;
			}
			if (ident == &__word___ident ||
			    ident == &word_ident) {
				ctype->modifiers |= MOD_LONG;
				return NULL;
			}
			return "unknown mode attribute";
		}
		return "expected attribute mode symbol";
	}

	/* Throw away for now.. */
	if (attribute == &format_ident ||
	    attribute == &__format___ident)
		return NULL;
	if (attribute == &section_ident ||
	    attribute == &__section___ident)
		return NULL;
	if (attribute == &unused_ident ||
	    attribute == &__unused___ident)
		return NULL;
	if (attribute == &const_ident ||
	    attribute == &__const_ident ||
	    attribute == &__const___ident)
		return NULL;
	if (attribute == &noreturn_ident ||
	    attribute == &__noreturn___ident)
		return NULL;
	if (attribute == &regparm_ident)
		return NULL;
	if (attribute == &weak_ident)
		return NULL;
	if (attribute == &alias_ident)
		return NULL;
	if (attribute == &pure_ident)
		return NULL;
	if (attribute == &always_inline_ident)
		return NULL;
	if (attribute == &syscall_linkage_ident)
		return NULL;
	if (attribute == &visibility_ident)
		return NULL;
	if (attribute == &model_ident ||
	    attribute == &__model___ident)
		return NULL;

	return "unknown attribute";
}

static struct token *attribute_specifier(struct token *token, struct ctype *ctype)
{
	ctype->modifiers = 0;
	token = expect(token, '(', "after attribute");
	token = expect(token, '(', "after attribute");

	for (;;) {
		const char *error;
		struct ident *attribute_name;
		struct expression *attribute_expr;

		if (eof_token(token))
			break;
		if (match_op(token, ';'))
			break;
		if (token_type(token) != TOKEN_IDENT)
			break;
		attribute_name = token->ident;
		token = token->next;
		attribute_expr = NULL;
		if (match_op(token, '('))
			token = parens_expression(token, &attribute_expr, "in attribute");
		error = handle_attribute(ctype, attribute_name, attribute_expr);
		if (error)
			warning(token->pos, "attribute '%s': %s", show_ident(attribute_name), error);
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
			warning(pos, "Just how %sdo you want this type to be?",
				modifier_string(dup));

		conflict = !(~mod & ~old & (MOD_LONG | MOD_SHORT));
		if (conflict)
			warning(pos, "You cannot have both long and short modifiers.");

		conflict = !(~mod & ~old & (MOD_SIGNED | MOD_UNSIGNED));
		if (conflict)
			warning(pos, "You cannot have both signed and unsigned modifiers.");

		// Only one storage modifier allowed, except that "inline" doesn't count.
		conflict = (mod | old) & (MOD_STORAGE & ~MOD_INLINE);
		conflict &= (conflict - 1);
		if (conflict)
			warning(pos, "multiple storage classes");

		ctype->modifiers = old | mod | extra;
	}

	/* Context mask and value */
	ctype->in_context += thistype->in_context;
	ctype->out_context += thistype->out_context;

	/* Alignment */
	if (thistype->alignment & (thistype->alignment-1)) {
		warning(pos, "I don't like non-power-of-2 alignments");
		thistype->alignment = 0;
	}
	if (thistype->alignment > ctype->alignment)
		ctype->alignment = thistype->alignment;

	/* Address space */
	ctype->as = thistype->as;
}

static void check_modifiers(struct position *pos, struct symbol *s, unsigned long mod)
{
	unsigned long banned, wrong;
	unsigned long this_mod = s->ctype.modifiers;
	const unsigned long BANNED_SIZE = MOD_LONG | MOD_LONGLONG | MOD_SHORT;
	const unsigned long BANNED_SIGN = MOD_SIGNED | MOD_UNSIGNED;

	if (this_mod & (MOD_STRUCTOF | MOD_UNIONOF | MOD_ENUMOF))
		banned = BANNED_SIZE | BANNED_SIGN;
	else if (this_mod & MOD_SPECIALBITS)
		banned = 0;
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
		warning(*pos, "modifier %sis invalid in this context",
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
		if (qual && (mod & ~(MOD_ATTRIBUTE | MOD_CONST | MOD_VOLATILE)))
			break;
		if (mod & MOD_SPECIALBITS) {
			if (mod & MOD_STRUCTOF)
				next = struct_or_union_specifier(SYM_STRUCT, next, &thistype);
			else if (mod & MOD_UNIONOF)
				next = struct_or_union_specifier(SYM_UNION, next, &thistype);
			else if (mod & MOD_ENUMOF)
				next = enum_specifier(next, &thistype);
			else if (mod & MOD_ATTRIBUTE)
				next = attribute_specifier(next, &thistype);
			else if (mod & MOD_TYPEOF)
				next = typeof_specifier(next, &thistype);
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

	/* Turn the "virtual types" into real types with real sizes etc */
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
			warning(token->pos, "invalid modifier");
			return token;
		}
		type = alloc_symbol(token->pos, SYM_BASETYPE);
		*type = *ctype->base_type;
		type->ctype.base_type = ctype->base_type;
		type->type = SYM_RESTRICT;
		type->ctype.modifiers &= ~MOD_SPECIFIER;
		ctype->base_type = type;
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

static struct token *parameter_type_list(struct token *, struct symbol *);
static struct token *declarator(struct token *token, struct symbol *sym, struct ident **p);

static struct token *handle_attributes(struct token *token, struct ctype *ctype)
{
	for (;;) {
		if (token_type(token) != TOKEN_IDENT)
			break;
		if (match_idents(token, &__attribute___ident, &__attribute_ident, NULL)) {
			struct ctype thistype = { 0, };
			token = attribute_specifier(token->next, &thistype);
			apply_ctype(token->pos, &thistype, ctype);
			continue;
		}
		if (match_idents(token, &asm_ident, &__asm_ident, &__asm___ident)) {
			struct expression *expr;
			token = expect(token->next, '(', "after asm");
			token = parse_expression(token->next, &expr);
			token = expect(token, ')', "after asm");
			continue;
		}
		break;
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
		token = handle_attributes(token, ctype);

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
			int fn = (p && *p) || match_op(next, ')') || lookup_type(next);

			if (!fn) {
				struct symbol *base_type = ctype->base_type;
				token = declarator(next, decl, p);
				token = expect(token, ')', "in nested declarator");
				while (ctype->base_type != base_type)
					ctype = &ctype->base_type->ctype;
				p = NULL;
				continue;
			}

			sym = indirect(token->pos, ctype, SYM_FN);
			token = parameter_type_list(next, sym);
			token = expect(token, ')', "in function declarator");
			continue;
		}
		if (token->special == '[') {
			struct symbol *array = indirect(token->pos, ctype, SYM_ARRAY);
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

	modifiers = ctype->modifiers & ~(MOD_TYPEDEF | MOD_ATTRIBUTE);
	base_type = ctype->base_type;
	ctype->modifiers = modifiers;
	
	while (match_op(token,'*')) {
		struct symbol *ptr = alloc_symbol(token->pos, SYM_PTR);
		ptr->ctype.modifiers = modifiers & ~MOD_STORAGE;
		ptr->ctype.as = ctype->as;
		ptr->ctype.in_context += ctype->in_context;
		ptr->ctype.out_context += ctype->out_context;
		ptr->ctype.base_type = base_type;

		base_type = ptr;
		ctype->modifiers = modifiers & MOD_STORAGE;
		ctype->base_type = base_type;
		ctype->as = 0;
		ctype->in_context = 0;
		ctype->out_context = 0;

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

	if (!is_int_type(ctype->base_type)) {
		warning(token->pos, "invalid bitfield specifier for type %s.",
			show_typename(ctype->base_type));
		// Parse this to recover gracefully.
		return conditional_expression(token->next, &expr);
	}

	bitfield = indirect(token->pos, ctype, SYM_BITFIELD);
	token = conditional_expression(token->next, &expr);
	width = get_expression_value(expr);
	bitfield->bit_size = width;

	if (width < 0 || width > INT_MAX) {
		warning(token->pos, "invalid bitfield width, %lld.", width);
		width = -1;
	} else if (decl->ident && width == 0) {
		warning(token->pos, "invalid named zero-width bitfield `%s'",
		     show_ident(decl->ident));
		width = -1;
	} else if (decl->ident) {
		struct symbol *base_type = bitfield->ctype.base_type;
		int is_signed = !(base_type->ctype.modifiers & MOD_UNSIGNED);
		if (width == 1 && is_signed) {
			// Valid values are either {-1;0} or {0}, depending on integer
			// representation.  The latter makes for very efficient code...
			warning(token->pos, "dubious one-bit signed bitfield");
		}
		if (Wdefault_bitfield_sign &&
		    base_type->type != SYM_ENUM &&
		    !(base_type->ctype.modifiers & MOD_EXPLICITLY_SIGNED) &&
		    is_signed) {
			// The sign of bitfields is unspecified by default.
			warning (token->pos, "dubious bitfield without explicit `signed' or `unsigned'");
		}
	}
	bitfield->bit_size = width;
	return token;
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list)
{
	while (!match_op(token, '}')) {
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
				token = handle_attributes(token, &decl->ctype);
			}
			add_symbol(list, decl);
			if (!match_op(token, ','))
				break;
			token = token->next;
		}
		if (!match_op(token, ';')) {
			warning(token->pos, "expected ; at end of declaration");
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
	return token;
}

struct token *typename(struct token *token, struct symbol **p)
{
	struct symbol *sym = alloc_symbol(token->pos, SYM_NODE);
	*p = sym;
	token = declaration_specifiers(token, &sym->ctype, 0);
	return declarator(token, sym, NULL);
}

struct token *expression_statement(struct token *token, struct expression **tree)
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

static struct token *parse_asm(struct token *token, struct statement *stmt)
{
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
		warning(stmt->pos, "not in switch scope");
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
	stmt->iterator_post_condition = e2;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	return token;
}

struct token *parse_while_statement(struct token *token, struct statement *stmt)
{
	struct expression *expr;
	struct statement *iterator;

	start_iterator(stmt);
	token = parens_expression(token->next, &expr, "after 'while'");
	token = statement(token, &iterator);

	stmt->iterator_pre_condition = expr;
	stmt->iterator_post_condition = expr;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	return token;
}

struct token *parse_do_statement(struct token *token, struct statement *stmt)
{
	struct expression *expr;
	struct statement *iterator;

	start_iterator(stmt);
	token = statement(token->next, &iterator);
	if (token_type(token) == TOKEN_IDENT && token->ident == &while_ident)
		token = token->next;
	else
		warning(token->pos, "expected 'while' after 'do'");
	token = parens_expression(token, &expr, "after 'do-while'");

	stmt->iterator_post_condition = expr;
	stmt->iterator_statement = iterator;
	end_iterator(stmt);

	return expect(token, ';', "after statement");
}

static struct token *statement(struct token *token, struct statement **tree)
{
	struct statement *stmt = alloc_statement(token->pos, STMT_NONE);

	*tree = stmt;
	if (token_type(token) == TOKEN_IDENT) {
		if (token->ident == &if_ident) {
			stmt->type = STMT_IF;
			token = parens_expression(token->next, &stmt->if_conditional, "after if");
			token = statement(token, &stmt->if_true);
			if (token_type(token) != TOKEN_IDENT)
				return token;
			if (token->ident != &else_ident)
				return token;
			return statement(token->next, &stmt->if_false);
		}

		if (token->ident == &return_ident)
			return parse_return_statement(token, stmt);

		if (token->ident == &break_ident || token->ident == &continue_ident) {
			struct symbol *target = lookup_symbol(token->ident, NS_ITERATOR);
			stmt->type = STMT_GOTO;
			stmt->goto_label = target;
			if (!target)
				warning(stmt->pos, "break/continue not in iterator scope");
			return expect(token->next, ';', "at end of statement");
		}
		if (token->ident == &default_ident) {
			token = token->next;
			goto default_statement;
		}
		if (token->ident == &case_ident) {
			token = parse_expression(token->next, &stmt->case_expression);
			if (match_op(token, SPECIAL_ELLIPSIS))
				token = parse_expression(token->next, &stmt->case_to);
default_statement:
			stmt->type = STMT_CASE;
			token = expect(token, ':', "after default/case");
			add_case_statement(stmt);
			return statement(token, &stmt->case_statement);
		}
		if (token->ident == &switch_ident) {
			stmt->type = STMT_SWITCH;
			start_switch(stmt);
			token = parens_expression(token->next, &stmt->switch_expression, "after 'switch'");
			token = statement(token, &stmt->switch_statement);
			end_switch(stmt);
			return token;
		}
		if (token->ident == &for_ident)
			return parse_for_statement(token, stmt);

		if (token->ident == &while_ident)
			return parse_while_statement(token, stmt);

		if (token->ident == &do_ident)
			return parse_do_statement(token, stmt);

		if (token->ident == &goto_ident) {
			stmt->type = STMT_GOTO;
			token = token->next;
			if (match_op(token, '*')) {
				token = parse_expression(token->next, &stmt->goto_expression);
				add_statement(&function_computed_goto_list, stmt);
			} else if (token_type(token) == TOKEN_IDENT) {
				stmt->goto_label = label_symbol(token);
				token = token->next;
			} else {
				warning(token->pos, "Expected identifier or goto expression");
			}
			return expect(token, ';', "at end of statement");
		}
		if (match_idents(token, &asm_ident, &__asm___ident, &__asm_ident, NULL)) {
			return parse_asm(token->next, stmt);
		}
		if (token->ident == &__context___ident) {
			stmt->type = STMT_INTERNAL;
			token = parse_expression(token->next, &stmt->expression);
			return expect(token, ';', "at end of statement");
		}
		if (match_op(token->next, ':')) {
			stmt->type = STMT_LABEL;
			stmt->label_identifier = label_symbol(token);
			return statement(token->next->next, &stmt->label_statement);
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

static struct token * statement_list(struct token *token, struct statement_list **list, struct symbol_list **syms)
{
	for (;;) {
		struct statement * stmt;
		if (eof_token(token))
			break;
		if (match_op(token, '}'))
			break;
		if (lookup_type(token)) {
			if (warn_on_mixed && *list)
				warning(token->pos, "mixing declarations and code");
			token = external_declaration(token, syms);
			continue;
		}
		token = statement(token, &stmt);
		add_statement(list, stmt);
	}
	return token;
}

static struct token *parameter_type_list(struct token *token, struct symbol *fn)
{
	struct symbol_list **list = &fn->arguments;

	if (match_op(token, ')')) {
		// No warning for "void oink ();"
		// Bug or feature: warns for "void oink () __attribute__ ((noreturn));"
		if (!match_op(token->next, ';'))
			warning(token->pos, "non-ANSI function declaration");
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
	token = statement_list(token, &stmt->stmts, &stmt->syms);
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
		warning(sym->pos, "no identifier for function argument");
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
			warning(decl->pos, "function with external linkage has definition");
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
			warning(decl->pos, "function has computed goto but no targets?");
		else {
			struct statement *stmt;
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
		warning(arg->pos, "missing type declaration for parameter '%s'", show_ident(arg->ident));
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

	warning(token->pos, "non-ANSI function declaration");
	do {
		token = external_declaration(token, &args);
	} while (lookup_type(token));

	apply_k_r_types(args, decl);

	if (!match_op(token, '{')) {
		warning(token->pos, "expected function body");
		return token;
	}
	return parse_function_body(token, decl, list);
}


static struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct ident *ident = NULL;
	struct symbol *decl;
	struct ctype ctype = { 0, };
	struct symbol *base_type;
	int is_typedef;

	/* Top-level inline asm? */
	if (match_idents(token, &asm_ident, &__asm___ident, &__asm_ident, NULL)) {
		struct symbol_list **old_symbol_list;
		struct symbol *anon = alloc_symbol(token->pos, SYM_NODE);
		struct symbol *fn = alloc_symbol(token->pos, SYM_FN);
		struct statement *stmt;

		anon->ctype.base_type = fn;
		old_symbol_list = function_symbol_list;
		function_symbol_list = &anon->symbol_list;
		stmt = start_function(anon);
		token = parse_asm(token->next, stmt);
		end_function(anon);
		function_symbol_list = old_symbol_list;
		add_symbol(list, anon);
		return token;
	}

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype, 0);
	decl = alloc_symbol(token->pos, SYM_NODE);
	decl->ctype = ctype;
	token = declarator(token, decl, &ident);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	/* type define declaration? */
	is_typedef = (ctype.modifiers & MOD_TYPEDEF) != 0;

	/* Typedef's don't have meaningful storage */
	if (is_typedef) {
		ctype.modifiers &= ~MOD_STORAGE;
		decl->ctype.modifiers &= ~MOD_STORAGE;
		decl->ctype.modifiers |= MOD_USERTYPE;
	}

	bind_symbol(decl, ident, is_typedef ? NS_TYPEDEF: NS_SYMBOL);

	base_type = decl->ctype.base_type;
	if (!is_typedef && base_type && base_type->type == SYM_FN) {
		/* K&R argument declaration? */
		if (lookup_type(token))
			return parse_k_r_arguments(token, decl, list);
		if (match_op(token, '{'))
			return parse_function_body(token, decl, list);

		if (!(decl->ctype.modifiers & MOD_STATIC))
			decl->ctype.modifiers |= MOD_EXTERN;
	} else if (!is_typedef && base_type == &void_ctype && !(decl->ctype.modifiers & MOD_EXTERN)) {
		warning(token->pos, "void declaration");
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
		if (!ident) {
			warning(token->pos, "expected identifier name in type definition");
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

struct symbol_list *translation_unit(struct token *token)
{
	while (!eof_token(token))
		token = external_declaration(token, &translation_unit_used_list);
	// They aren't needed any more
	clear_token_alloc();

	/* Evaluate the symbol list */
	evaluate_symbol_list(translation_unit_used_list);
	return translation_unit_used_list;
}
