/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 */

#ifndef __GNUC__
typedef int __builtin_va_list;
#endif

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

struct statement *alloc_statement(struct token * token, int type)
{
	struct statement *stmt = __alloc_statement(0);
	stmt->type = type;
	return stmt;
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list);

static void force_default_type(struct ctype *type)
{
	if (!type->base_type)
		type->base_type = &int_type;
}

static struct symbol *indirect(struct symbol *base, int type)
{
	struct symbol *sym = alloc_symbol(NULL, type);

	sym->ctype.base_type = base;
	sym->ctype.modifiers = 0;
	force_default_type(&sym->ctype);
	return sym;
}

static struct symbol *lookup_or_create_symbol(enum namespace ns, enum type type, struct token *token)
{
	struct symbol *sym = lookup_symbol(token->ident, ns);
	if (!sym) {
		sym = alloc_symbol(token, type);
		bind_symbol(sym, token->ident, ns);
	}
	return sym;
}

struct token *struct_union_enum_specifier(enum namespace ns, enum type type,
	struct token *token, struct ctype *ctype,
	struct token *(*parse)(struct token *, struct symbol *))
{
	struct symbol *sym;

	if (token->type == TOKEN_IDENT) {
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
		warn(token, "expected declaration");
		ctype->base_type = &bad_type;
		return token;
	}

	sym = alloc_symbol(token, type);
	token = parse(token->next, sym);
	ctype->base_type = sym->ctype.base_type;
	ctype->modifiers = sym->ctype.modifiers;
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

/* FIXME! */
static struct token *parse_enum_declaration(struct token *token, struct symbol *sym)
{
	return skip_to(token, '}');
}

struct token *enum_specifier(struct token *token, struct ctype *ctype)
{
	return struct_union_enum_specifier(NS_ENUM, SYM_ENUM, token, ctype, parse_enum_declaration);
}

struct token *typeof_specifier(struct token *token, struct ctype *ctype)
{
	struct symbol *sym;
	struct expression *expr;

	if (match_op(token, '(') && lookup_type(token->next)) {
		token = typename(token->next, &sym);
		*ctype = sym->ctype;
		return expect(token, ')', "after typeof");
	}
	token = parse_expression(token, &expr);
	/* look at type.. */
	ctype->base_type = &bad_type;
	return token;
}

struct token *attribute_specifier(struct token *token, struct ctype *ctype)
{
	int parens = 0;

	token = expect(token, '(', "after attribute");
	token = expect(token, '(', "after attribute");

	for (;;) {
		if (eof_token(token))
			break;
		if (match_op(token, ';'))
			break;
		if (match_op(token, ')')) {
			if (!parens)
				break;
			parens--;
		}
		if (match_op(token, '('))
			parens++;
		token = token->next;
	}

	token = expect(token, ')', "after attribute");
	token = expect(token, ')', "after attribute");
	return token;
}

#define MOD_SPECIALBITS (SYM_STRUCTOF | SYM_UNIONOF | SYM_ENUMOF | SYM_ATTRIBUTE | SYM_TYPEOF)

static struct token *declaration_specifiers(struct token *next, struct ctype *ctype)
{
	struct token *token;

	while ( (token = next) != NULL ) {
		struct ctype thistype;
		struct ident *ident;
		struct symbol *s, *type;
		unsigned long mod;

		next = token->next;
		if (token->type != TOKEN_IDENT)
			break;
		ident = token->ident;

		s = lookup_symbol(ident, NS_TYPEDEF);
		if (!s)
			break;
		thistype = s->ctype;
		if (thistype.modifiers & MOD_SPECIALBITS) {
			unsigned long mod = thistype.modifiers;
			thistype.modifiers = mod & ~MOD_SPECIALBITS;
			if (mod & SYM_STRUCTOF)
				next = struct_or_union_specifier(SYM_STRUCT, next, &thistype);
			else if (mod & SYM_UNIONOF)
				next = struct_or_union_specifier(SYM_UNION, next, &thistype);
			else if (mod & SYM_ENUMOF)
				next = enum_specifier(next, &thistype);
			else if (mod & SYM_ATTRIBUTE)
				next = attribute_specifier(next, &thistype);
			else if (mod & SYM_TYPEOF)
				next = typeof_specifier(next, &thistype);
		}

		type = thistype.base_type;
		mod = thistype.modifiers;
		if (type) {
			if (type != ctype->base_type) {
				if (ctype->base_type) {
					warn(token, "Strange mix of types");
					continue;
				}
				ctype->base_type = type;
			}
		}
		if (mod) {
			unsigned long old = ctype->modifiers;
			unsigned long extra = 0, dup;

			if (mod & old & SYM_LONG) {
				extra = SYM_LONGLONG | SYM_LONG;
				mod &= ~SYM_LONG;
				old &= ~SYM_LONG;
			}
			dup = (mod & old) | (extra & old) | (extra & mod);
			if (dup)
				warn(token, "Just how %s do you want this type to be?",
					modifier_string(dup));
			ctype->modifiers = old | mod | extra;
		}
	}
	return token;
}

static int constant_value(struct expression *expr)
{
	return 0;
}

static struct token *abstract_array_declarator(struct token *token, struct symbol **tree)
{
	struct expression *expr;
	struct symbol *sym = *tree;
	token = parse_expression(token, &expr);
	sym->size = constant_value(expr);
	return token;
}

static struct token *abstract_function_declarator(struct token *, struct symbol_list **);
static struct token *declarator(struct token *token, struct symbol **tree, struct token **p);

static struct token *direct_declarator(struct token *token, struct symbol **tree, struct token **p)
{
	if (p && token->type == TOKEN_IDENT) {
		if (lookup_symbol(token->ident, NS_TYPEDEF)) {
			warn(token, "unexpected type/qualifier");
			return token;
		}
		*p = token;
		token = token->next;
	}

	for (;;) {
		if (match_ident(token, &__attribute___ident) || match_ident(token, &__attribute_ident)) {
			struct ctype ctype = { 0, };
			token = attribute_specifier(token->next, &ctype);
			continue;
		}
		if (token->type != TOKEN_SPECIAL)
			return token;

		/*
		 * This can be either a parameter list or a grouping.
		 * For the direct (non-abstract) case, we know if must be
		 * a paramter list if we already saw the identifier.
		 * For the abstract case, we know if must be a parameter
		 * list if it is empty or starts with a type.
		 */
		if (token->special == '(') {
			struct token *next = token->next;
			int fn = (p && *p) || match_op(next, ')') || lookup_type(next);

			if (!fn) {
				token = declarator(next, tree, p);
				token = expect(token, ')', "in nested declarator");
				continue;
			}
			
			*tree = indirect(*tree, SYM_FN);
			token = abstract_function_declarator(next, &(*tree)->arguments);
			token = expect(token, ')', "in function declarator");
			continue;
		}
		if (token->special == '[') {
			*tree = indirect(*tree, SYM_ARRAY);
			token = abstract_array_declarator(token->next, tree);
			token = expect(token, ']', "in abstract_array_declarator");
			continue;
		}
		if (token->special == ':') {
			struct expression *expr;
			*tree = indirect(*tree, SYM_BITFIELD);
			token = conditional_expression(token->next, &expr);
			continue;
		}
		break;
	}
	if (p) {
		(*tree)->token = *p;
		(*tree)->ident = *p;
	}
	return token;
}

static struct token *pointer(struct token *token, struct symbol **tree, struct ctype *ctype)
{
	force_default_type(ctype);
	(*tree)->ctype = *ctype;
	while (match_op(token,'*')) {
		struct symbol *sym;
		sym = alloc_symbol(token, SYM_PTR);
		sym->ctype.base_type = *tree;
		*tree = sym;
		token = declaration_specifiers(token->next, &sym->ctype);
	}
	return token;
}

static struct token *declarator(struct token *token, struct symbol **tree, struct token **p)
{
	token = pointer(token, tree, &(*tree)->ctype);
	return direct_declarator(token, tree, p);
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list)
{
	for (;;) {
		struct ctype ctype = {0, };
	
		token = declaration_specifiers(token, &ctype);
		for (;;) {
			struct symbol *spec = alloc_symbol(token, SYM_TYPE);
			struct ctype *base_type = &ctype;
			struct symbol *declaration = spec;
			struct token *ident = NULL;
			token = pointer(token, &declaration, base_type);
			token = direct_declarator(token, &declaration, &ident);
			if (match_op(token, ':')) {
				struct expression *expr;
				token = parse_expression(token->next, &expr);
			}
			add_symbol(list, declaration);
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
	struct token *ident = NULL;
	struct ctype ctype = { 0, }, *base_type;

	token = declaration_specifiers(token, &ctype);
	*tree = alloc_symbol(token, SYM_TYPE);
	base_type = &ctype;
	token = pointer(token, tree, base_type);
	token = direct_declarator(token, tree, &ident);
	return token;
}

struct token *typename(struct token *token, struct symbol **p)
{
	struct ctype ctype = { 0, };
	struct symbol *sym = alloc_symbol(token, SYM_TYPE);
	*p = sym;
	token = declaration_specifiers(token, &ctype);
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

struct token *statement(struct token *token, struct statement **tree)
{
	struct statement *stmt = alloc_statement(token, STMT_NONE);

	*tree = stmt;
	if (token->type == TOKEN_IDENT) {
		if (token->ident == &if_ident) {
			stmt->type = STMT_IF;
			token = parens_expression(token->next, &stmt->if_conditional, "after if");
			token = statement(token, &stmt->if_true);
			if (token->type != TOKEN_IDENT)
				return token;
			if (token->ident != &else_ident)
				return token;
			return statement(token->next, &stmt->if_false);
		}
		if (token->ident == &return_ident) {
			stmt->type = STMT_RETURN;
			return expression_statement(token->next, &stmt->expression);
		}
		if (token->ident == &break_ident) {
			stmt->type = STMT_BREAK;
			return expect(token->next, ';', "at end of statement");
		}
		if (token->ident == &continue_ident) {
			stmt->type = STMT_CONTINUE;
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
			return statement(token, &stmt->case_statement);
		}
		if (token->ident == &switch_ident) {
			stmt->type = STMT_SWITCH;
			token = parens_expression(token->next, &stmt->switch_expression, "after 'switch'");
			return statement(token, &stmt->switch_statement);
		}
		if (token->ident == &for_ident) {
			stmt->type = STMT_FOR;
			token = expect(token->next, '(', "after 'for'");
			token = parse_expression(token, &stmt->e1);
			token = expect(token, ';', "in 'for'");
			token = parse_expression(token, &stmt->e2);
			token = expect(token, ';', "in 'for'");
			token = parse_expression(token, &stmt->e3);
			token = expect(token, ')', "in 'for'");
			return statement(token, &stmt->iterate);
		}
		if (token->ident == &while_ident) {
			stmt->type = STMT_WHILE;
			token = parens_expression(token->next, &stmt->e1, "after 'while'");
			return statement(token, &stmt->iterate);
		}
		if (token->ident == &do_ident) {
			stmt->type = STMT_DO;
			token = statement(token->next, &stmt->iterate);
			if (token->type == TOKEN_IDENT && token->ident == &while_ident)
				token = token->next;
			else
				warn(token, "expected 'while' after 'do'");
			token = parens_expression(token, &stmt->e1, "after 'do-while'");
			return expect(token, ';', "after statement");
		}
		if (token->ident == &goto_ident) {
			stmt->type = STMT_GOTO;
			token = token->next;			
			if (token->type == TOKEN_IDENT) {
				stmt->goto_label = token;
				token = token->next;
			} else
				warn(token, "invalid label");
			return expect(token, ';', "at end of statement");
		}
		if (token->ident == &asm_ident || token->ident == &__asm___ident || token->ident == &__asm_ident) {
			struct expression *expr;
			stmt->type = STMT_ASM;
			token = token->next;
			if (token->type == TOKEN_IDENT) {
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
			stmt->label_identifier = token;
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

static struct token *parameter_type_list(struct token *token, struct symbol_list **list)
{
	for (;;) {
		struct symbol *sym = alloc_symbol(token, SYM_TYPE);

		token = parameter_declaration(token, &sym);
		add_symbol(list, sym);
		if (!match_op(token, ','))
			break;
		token = token->next;

		if (match_op(token, SPECIAL_ELLIPSIS)) {
			/* FIXME: mark the function */
			token = token->next;
			break;
		}
	}
	return token;
}

static struct token *abstract_function_declarator(struct token *token, struct symbol_list **list)
{
	return parameter_type_list(token, list);
}

static struct token *external_declaration(struct token *token, struct symbol_list **list);

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

static struct token *initializer_list(struct token *token, struct ctype *type)
{
	for (;;) {
		token = initializer(token, type);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	return token;
}

struct token *initializer(struct token *token, struct ctype *type)
{
	struct expression *expr;
	if (match_op(token, '{')) {
		token = initializer_list(token->next, type);
		return expect(token, '}', "at end of initializer");
	}
	return assignment_expression(token, &expr);
}

static void declare_argument(struct symbol *sym, void *data, int flags)
{
	if (!sym->ident) {
		warn(sym->token, "no identifier for function argument");
		return;
	}
	bind_symbol(sym, sym->ident->ident, NS_SYMBOL);
}

static struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct token *ident = NULL;
	struct symbol *decl;
	struct ctype ctype = { 0, };

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype);

	decl = alloc_symbol(token, SYM_TYPE);
	token = pointer(token, &decl, &ctype);
	token = declarator(token, &decl, &ident);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	/* type define declaration? */
	if (ctype.modifiers & SYM_TYPEDEF) {
		ctype.modifiers &= ~SYM_TYPEDEF;
		bind_symbol(indirect(decl, SYM_TYPEDEF), ident->ident, NS_TYPEDEF);
		return expect(token, ';', "end of typedef");
	}

	decl->ident = ident;
	add_symbol(list, decl);
	bind_symbol(decl, ident->ident, NS_SYMBOL);

	if (decl->type == SYM_FN && match_op(token, '{')) {
		decl->stmt = alloc_statement(token, STMT_COMPOUND);
		start_symbol_scope();
		symbol_iterate(decl->arguments, declare_argument, NULL);
		token = compound_statement(token->next, decl->stmt);
		end_symbol_scope();
		return expect(token, '}', "at end of function");
	}

	for (;;) {
		if (match_op(token, '='))
			token = initializer(token->next, &decl->ctype);
		if (!match_op(token, ','))
			break;

		ident = NULL;
		decl = alloc_symbol(token, SYM_TYPE);
		token = pointer(token, &decl, &ctype);
		token = declarator(token->next, &decl, &ident);
		if (!ident) {
			warn(token, "expected identifier name in type definition");
			return token;
		}

		add_symbol(list, decl);
		bind_symbol(decl, ident->ident, NS_SYMBOL);
	}
	return expect(token, ';', "at end of declaration");
}

void translation_unit(struct token *token, struct symbol_list **list)
{
	while (!eof_token(token))
		token = external_declaration(token, list);
}
