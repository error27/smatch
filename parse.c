/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 *
 * Copyright (C) 2003 Transmeta Corp.
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

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"
#include "target.h"

static struct symbol_list **function_symbol_list;

// Add a symbol to the list of function-local symbols
#define fn_local_symbol(x) add_symbol(function_symbol_list, (x))

static struct token *statement(struct token *token, struct statement **tree);
static struct token *external_declaration(struct token *token, struct symbol_list **list);

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
		sym->ident = token->ident;
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

struct token *struct_union_enum_specifier(enum namespace ns, enum type type,
	struct token *token, struct ctype *ctype,
	struct token *(*parse)(struct token *, struct symbol *))
{
	struct symbol *sym;

	ctype->modifiers = 0;
	if (token_type(token) == TOKEN_IDENT) {
		sym = lookup_or_create_symbol(ns, type, token);
		token = token->next;
		ctype->base_type = sym;
		if (match_op(token, '{')) {
			token = parse(token->next, sym);
			token = expect(token, '}', "at end of struct-union-enum-specifier");
		}
		return token;
	}

	// private struct/union/enum type
	if (!match_op(token, '{')) {
		warn(token->pos, "expected declaration");
		ctype->base_type = &bad_type;
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
	return struct_union_enum_specifier(NS_STRUCT, type, token, ctype, parse_struct_declaration);
}

static struct token *parse_enum_declaration(struct token *token, struct symbol *parent)
{
	int nextval = 0;
	while (token_type(token) == TOKEN_IDENT) {
		struct token *next = token->next;
		struct symbol *sym;

		sym = alloc_symbol(token->pos, SYM_ENUM);
		bind_symbol(sym, token->ident, NS_SYMBOL);
		sym->ctype.base_type = parent;

		if (match_op(next, '=')) {
			struct expression *expr;
			next = constant_expression(next->next, &expr);
			nextval = get_expression_value(expr);
		}
		sym->value = nextval;

		token = next;
		if (!match_op(token, ','))
			break;
		token = token->next;
		nextval = nextval + 1;
	}
	return token;
}

struct token *enum_specifier(struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(NS_ENUM, SYM_ENUM, token, ctype, parse_enum_declaration);
}

struct token *typeof_specifier(struct token *token, struct ctype *ctype)
{
	struct symbol *sym;

	if (!match_op(token, '(')) {
		warn(token->pos, "expected '(' after typeof");
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
	if (match_string_ident(attribute, "packed") ||
	    match_string_ident(attribute, "__packed__")) {
		ctype->alignment = 1;
		return NULL;
	}
	if (match_string_ident(attribute, "aligned") ||
	    match_string_ident(attribute, "__aligned__")) {
		int alignment = MAX_ALIGNMENT;
		if (expr)
			alignment = get_expression_value(expr);
		ctype->alignment = alignment;
		return NULL;
	}
	if (match_string_ident(attribute, "nocast")) {
		ctype->modifiers |= MOD_NOCAST;
		return NULL;
	}
	if (match_string_ident(attribute, "noderef")) {
		ctype->modifiers |= MOD_NODEREF;
		return NULL;
	}
	if (match_string_ident(attribute, "address_space")) {
		if (!expr)
			return "expected address space number";
		ctype->as = get_expression_value(expr);
		return NULL;
	}
	if (match_string_ident(attribute, "context")) {
		if (expr->type == EXPR_COMMA) {
			int mask = get_expression_value(expr->left);
			int value = get_expression_value(expr->right);
			if (value & ~mask)
				return "nonsense attribute types";
			ctype->contextmask |= mask;
			ctype->context |= value;
			return NULL;
		}
		return "expected context mask and value";
	}

	/* Throw away for now.. */
	if (match_string_ident(attribute, "format") ||
	    match_string_ident(attribute, "__format__"))
		return NULL;
	if (match_string_ident(attribute, "section") ||
	    match_string_ident(attribute, "__section__"))
		return NULL;
	if (match_string_ident(attribute, "unused") ||
	    match_string_ident(attribute, "__unused__"))
		return NULL;
	if (match_string_ident(attribute, "const") ||
	    match_string_ident(attribute, "__const__"))
		return NULL;
	if (match_string_ident(attribute, "noreturn"))
		return NULL;
	if (match_string_ident(attribute, "regparm"))
		return NULL;
	if (match_string_ident(attribute, "weak"))
		return NULL;

	return "unknown attribute";
}

struct token *attribute_specifier(struct token *token, struct ctype *ctype)
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
			warn(token->pos, "attribute '%s': %s", show_ident(attribute_name), error);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}

	token = expect(token, ')', "after attribute");
	token = expect(token, ')', "after attribute");
	return token;
}

#define MOD_SPECIALBITS (MOD_STRUCTOF | MOD_UNIONOF | MOD_ENUMOF | MOD_ATTRIBUTE | MOD_TYPEOF)
#define MOD_SPECIFIER (MOD_CHAR | MOD_SHORT | MOD_LONG | MOD_LONGLONG | MOD_SIGNED | MOD_UNSIGNED)

struct symbol * ctype_integer(unsigned int spec)
{
	static struct symbol *const integer_ctypes[][2] = {
		{ &llong_ctype, &ullong_ctype },
		{ &long_ctype,  &ulong_ctype  },
		{ &short_ctype, &ushort_ctype },
		{ &char_ctype,  &uchar_ctype  },
		{ &int_ctype,   &uint_ctype   },
	};
	struct symbol *const (*ctype)[2];

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
	return ctype[0][(spec & MOD_UNSIGNED) != 0];
}

struct symbol * ctype_fp(unsigned int spec)
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
		unsigned long extra = 0, dup;

		if (mod & old & MOD_LONG) {
			extra = MOD_LONGLONG | MOD_LONG;
			mod &= ~MOD_LONG;
			old &= ~MOD_LONG;
		}
		dup = (mod & old) | (extra & old) | (extra & mod);
		if (dup)
			warn(pos, "Just how %sdo you want this type to be?",
				modifier_string(dup));
		ctype->modifiers = old | mod | extra;
	}

	/* Context mask and value */
	if ((ctype->context ^ thistype->context) & (ctype->contextmask & thistype->contextmask)) {
		warn(pos, "inconsistend attribute types");
		thistype->context = 0;
		thistype->contextmask = 0;
	}
	ctype->context |= thistype->context;
	ctype->contextmask |= thistype->contextmask;

	/* Alignment */
	if (thistype->alignment & (thistype->alignment-1)) {
		warn(pos, "I don't like non-power-of-2 alignments");
		thistype->alignment = 0;
	}
	if (thistype->alignment > ctype->alignment)
		ctype->alignment = thistype->alignment;

	/* Address space */
	ctype->as = thistype->as;
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
			if (type != ctype->base_type) {
				if (ctype->base_type)
					break;
				ctype->base_type = type;
			}
		}

		apply_ctype(token->pos, &thistype, ctype);
	}

	/* Turn the "virtual types" into real types with real sizes etc */
	if (!ctype->base_type && (ctype->modifiers & MOD_SPECIFIER))
		ctype->base_type = &int_type;

	if (ctype->base_type == &int_type) {
		ctype->base_type = ctype_integer(ctype->modifiers & MOD_SPECIFIER);
		ctype->modifiers &= ~MOD_SPECIFIER;
		return token;
	}
	if (ctype->base_type == &fp_type) {
		ctype->base_type = ctype_fp(ctype->modifiers & MOD_SPECIFIER);
		ctype->modifiers &= ~MOD_SPECIFIER;
		return token;
	}
	return token;
}

static struct token *abstract_array_declarator(struct token *token, struct symbol *sym)
{
	struct expression *expr = NULL;

	token = parse_expression(token, &expr);
	if (expr)
		sym->array_size = get_expression_value(expr);
	else
		sym->array_size = -1;
	return token;
}

static struct token *parameter_type_list(struct token *, struct symbol *);
static struct token *declarator(struct token *token, struct symbol **tree, struct ident **p);

static struct token *direct_declarator(struct token *token, struct symbol **tree, struct ident **p)
{
	struct ctype *ctype = &(*tree)->ctype;

	if (p && token_type(token) == TOKEN_IDENT) {
		*p = token->ident;
		token = token->next;
	}

	for (;;) {
		if (match_ident(token, &__attribute___ident) || match_ident(token, &__attribute_ident)) {
			struct ctype thistype = { 0, };
			token = attribute_specifier(token->next, &thistype);
			apply_ctype(token->pos, &thistype, ctype);
			continue;
		}
		if (token_type(token) != TOKEN_SPECIAL)
			return token;

		/*
		 * This can be either a parameter list or a grouping.
		 * For the direct (non-abstract) case, we know if must be
		 * a paramter list if we already saw the identifier.
		 * For the abstract case, we know if must be a parameter
		 * list if it is empty or starts with a type.
		 */
		if (token->special == '(') {
			struct symbol *sym;
			struct token *next = token->next;
			int fn = (p && *p) || match_op(next, ')') || lookup_type(next);

			if (!fn) {
				struct symbol *base_type = ctype->base_type;
				token = declarator(next, tree, p);
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
		if (token->special == ':') {
			struct symbol *bitfield;
			struct expression *expr;
			bitfield = indirect(token->pos, ctype, SYM_BITFIELD);
			token = conditional_expression(token->next, &expr);
			bitfield->fieldwidth = get_expression_value(expr);
			continue;
		}
		break;
	}
	if (p) {
		(*tree)->ident = *p;
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
		ptr->ctype.context = ctype->context;
		ptr->ctype.contextmask = ctype->contextmask;
		ptr->ctype.base_type = base_type;

		base_type = ptr;
		ctype->modifiers = modifiers & MOD_STORAGE;
		ctype->base_type = base_type;
		ctype->as = 0;
		ctype->context = 0;
		ctype->contextmask = 0;

		token = declaration_specifiers(token->next, ctype, 1);
	}
	return token;
}

static struct token *declarator(struct token *token, struct symbol **tree, struct ident **p)
{
	token = pointer(token, &(*tree)->ctype);
	return direct_declarator(token, tree, p);
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
			token = pointer(token, &decl->ctype);
			token = direct_declarator(token, &decl, &ident);
			if (match_op(token, ':')) {
				struct expression *expr;
				token = parse_expression(token->next, &expr);
			}
			add_symbol(list, decl);
			if (!match_op(token, ','))
				break;
			token = token->next;
		}
		if (!match_op(token, ';'))
			break;
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
	token = pointer(token, &sym->ctype);
	token = direct_declarator(token, tree, &ident);
	return token;
}

struct token *typename(struct token *token, struct symbol **p)
{
	struct symbol *sym = alloc_symbol(token->pos, SYM_NODE);
	*p = sym;
	token = declaration_specifiers(token, &sym->ctype, 0);
	return declarator(token, &sym, NULL);
}

struct token *expression_statement(struct token *token, struct expression **tree)
{
	token = parse_expression(token, tree);
	return expect(token, ';', "at end of statement");
}

static struct token *parse_asm_operands(struct token *token, struct statement *stmt)
{
	struct expression *expr;

	/* Allow empty operands */
	if (match_op(token->next, ':') || match_op(token->next, ')'))
		return token->next;
	do {
		token = primary_expression(token->next, &expr);
		token = parens_expression(token, &expr, "in asm parameter");
	} while (match_op(token, ','));
	return token;
}

static struct token *parse_asm_clobbers(struct token *token, struct statement *stmt)
{
	struct expression *expr;

	do {
		token = primary_expression(token->next, &expr);
	} while (match_op(token, ','));
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
	cont->ident = &continue_ident;
	bind_symbol(cont, &continue_ident, NS_ITERATOR);
	brk = alloc_symbol(stmt->pos, SYM_NODE);
	brk->ident = &break_ident;
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
	ret->ident = &return_ident;
	ret->ctype = sym->ctype.base_type->ctype;
	ret->ctype.modifiers &= ~(MOD_STORAGE | MOD_CONST | MOD_VOLATILE | MOD_INLINE | MOD_ADDRESSABLE | MOD_NOCAST | MOD_NODEREF | MOD_ACCESSED | MOD_TOPLEVEL);
	ret->ctype.modifiers |= (MOD_AUTO | MOD_REGISTER);
	bind_symbol(ret, &return_ident, NS_ITERATOR);
	stmt->ret = ret;

	fn_local_symbol(ret);
	return stmt;
}

static void end_function(struct symbol *sym)
{
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
	brk->ident = &break_ident;
	bind_symbol(brk, &break_ident, NS_ITERATOR);

	switch_case = alloc_symbol(stmt->pos, SYM_NODE);
	switch_case->ident = &case_ident;
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
		warn(stmt->pos, "switch with no cases");
	end_symbol_scope();
}

static void add_case_statement(struct statement *stmt)
{
	struct symbol *target = lookup_symbol(&case_ident, NS_ITERATOR);
	struct symbol *sym;

	if (!target) {
		warn(stmt->pos, "not in switch scope");
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
		error(token->pos, "internal error: return without a function target");
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
		warn(token->pos, "expected 'while' after 'do'");
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
				warn(stmt->pos, "break/continue not in iterator scope");
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
			} else if (token_type(token) == TOKEN_IDENT) {
				stmt->goto_label = label_symbol(token);
				token = token->next;
			} else {
				warn(token->pos, "Expected identifier or goto expression");
			}
			return expect(token, ';', "at end of statement");
		}
		if (token->ident == &asm_ident || token->ident == &__asm___ident || token->ident == &__asm_ident) {
			struct expression *expr;
			stmt->type = STMT_ASM;
			token = token->next;
			if (token_type(token) == TOKEN_IDENT) {
				if (token->ident == &__volatile___ident || token->ident == &volatile_ident)
					token = token->next;
			}
			token = expect(token, '(', "after asm");
			token = parse_expression(token->next, &expr);
			if (match_op(token, ':'))
				token = parse_asm_operands(token, stmt);
			if (match_op(token, ':'))
				token = parse_asm_operands(token, stmt);
			if (match_op(token, ':'))
				token = parse_asm_clobbers(token, stmt);
			token = expect(token, ')', "after asm");
			return expect(token, ';', "at end of asm-statement");
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

struct token * statement_list(struct token *token, struct statement_list **list)
{
	for (;;) {
		struct statement * stmt;
		if (eof_token(token))
			break;
		if (match_op(token, '}'))
			break;
		token = statement(token, &stmt);
		add_statement(list, stmt);
	}
	return token;
}

static struct token *parameter_type_list(struct token *token, struct symbol *fn)
{
	struct symbol_list **list = &fn->arguments;
	for (;;) {
		struct symbol *sym = alloc_symbol(token->pos, SYM_NODE);

		if (match_op(token, SPECIAL_ELLIPSIS)) {
			fn->variadic = 1;
			token = token->next;
			break;
		}
		
		if (!lookup_type(token)) {
			warn(token->pos, "non-ANSI parameter list");
			break;
		}
		token = parameter_declaration(token, &sym);
		/* Special case: (void) */
		if (!*list && !sym->ident && sym->ctype.base_type == &void_ctype)
			break;
		add_symbol(list, sym);
		if (!match_op(token, ','))
			break;
		token = token->next;

	}
	return token;
}

struct token *compound_statement(struct token *token, struct statement *stmt)
{
	while (!eof_token(token)) {
		if (!lookup_type(token))
			break;
		token = external_declaration(token, &stmt->syms);
	}
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
			warn(from->pos, "nonsense array initializer index range");
	}
	expr->idx_from = idx_from;
	expr->idx_to = idx_to;
	return expr;
}

static struct token *initializer_list(struct expression_list **list, struct token *token)
{
	for (;;) {
		struct token *next = token->next;
		struct expression *expr;

		if (match_op(token, '.') && (token_type(next) == TOKEN_IDENT) && match_op(next->next, '=')) {
			add_expression(list, identifier_expression(next));
			token = next->next->next;
		} else if ((token_type(token) == TOKEN_IDENT) && match_op(next, ':')) {
			add_expression(list, identifier_expression(token));
			token = next->next;
		} else if (match_op(token, '[')) {
			struct expression *from = NULL, *to = NULL;
			token = constant_expression(token->next, &from);
			if (match_op(token, SPECIAL_ELLIPSIS))
				token = constant_expression(token->next, &to);
			add_expression(list, index_expression(from, to));
			token = expect(token, ']', "at end of initializer index");
			token = expect(token, '=', "at end of initializer index");
		}

		expr = NULL;
		token = initializer(&expr, token);
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
		warn(sym->pos, "no identifier for function argument");
		return;
	}
	bind_symbol(sym, sym->ident, NS_SYMBOL);
}

static struct token *parse_function_body(struct token *token, struct symbol *decl,
	struct symbol_list **list)
{
	struct symbol *base_type = decl->ctype.base_type;
	struct statement *stmt;
	struct symbol *arg;

	function_symbol_list = &decl->symbol_list;
	if (decl->ctype.modifiers & MOD_EXTERN) {
		if (!(decl->ctype.modifiers & MOD_INLINE))
			warn(decl->pos, "function with external linkage has definition");
	}
	if (!(decl->ctype.modifiers & MOD_STATIC))
		decl->ctype.modifiers |= MOD_EXTERN;

	stmt = start_function(decl);

	base_type->stmt = stmt;
	FOR_EACH_PTR (base_type->arguments, arg) {
		declare_argument(arg, base_type);
	} END_FOR_EACH_PTR;

	token = compound_statement(token->next, stmt);

	end_function(decl);
	if (!(decl->ctype.modifiers & MOD_INLINE))
		add_symbol(list, decl);
	check_declaration(decl);
	function_symbol_list = NULL;
	return expect(token, '}', "at end of function");
}

static struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct ident *ident = NULL;
	struct symbol *decl;
	struct ctype ctype = { 0, };
	struct symbol *base_type;
	int is_typedef;

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype, 0);
	decl = alloc_symbol(token->pos, SYM_NODE);
	decl->ctype = ctype;
	token = pointer(token, &decl->ctype);
	token = declarator(token, &decl, &ident);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	decl->ident = ident;

	/* type define declaration? */
	is_typedef = (ctype.modifiers & MOD_TYPEDEF) != 0;

	/* Typedef's don't have meaningful storage */
	if (is_typedef) {
		ctype.modifiers &= ~MOD_STORAGE;
		decl->ctype.modifiers &= ~MOD_STORAGE;
	}

	bind_symbol(decl, ident, is_typedef ? NS_TYPEDEF: NS_SYMBOL);

	base_type = decl->ctype.base_type;
	if (!is_typedef && base_type && base_type->type == SYM_FN) {
		if (match_op(token, '{'))
			return parse_function_body(token, decl, list);

		if (!(decl->ctype.modifiers & MOD_STATIC))
			decl->ctype.modifiers |= MOD_EXTERN;
	}

	for (;;) {
		if (!is_typedef && match_op(token, '=')) {
			if (decl->ctype.modifiers & MOD_EXTERN) {
				warn(decl->pos, "symbol with external linkage has initializer");
				decl->ctype.modifiers &= ~MOD_EXTERN;
			}
			token = initializer(&decl->initializer, token->next);
		}
		if (!is_typedef) {
			if (!(decl->ctype.modifiers & (MOD_EXTERN | MOD_INLINE))) {
				add_symbol(list, decl);
				if (function_symbol_list)
					fn_local_symbol(decl);
			}
		}
		check_declaration(decl);

		if (!match_op(token, ','))
			break;

		ident = NULL;
		decl = alloc_symbol(token->pos, SYM_NODE);
		decl->ctype = ctype;
		token = pointer(token, &decl->ctype);
		token = declarator(token->next, &decl, &ident);
		if (!ident) {
			warn(token->pos, "expected identifier name in type definition");
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

void translation_unit(struct token *token, struct symbol_list **list)
{
	while (!eof_token(token))
		token = external_declaration(token, list);
	// They aren't needed any more
	clear_token_alloc();
}
