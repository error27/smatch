/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
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
	}
	return sym;
}

/*
 * NOTE! NS_LABEL is not just a different namespace,
 * it also ends up using function scope instead of the
 * regular symbol scope.
 */
static struct symbol *label_symbol(struct token *token)
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

		sym = alloc_symbol(token->pos, SYM_NODE);
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

static void handle_attribute(struct ctype *ctype, struct ident *attribute, struct expression *expr)
{
	if (match_string_ident(attribute, "packed")) {
		ctype->alignment = 1;
		return;
	}
	if (match_string_ident(attribute, "aligned")) {
		ctype->alignment = get_expression_value(expr);
		return;
	}
	if (match_string_ident(attribute, "nocast")) {
		ctype->modifiers |= MOD_NOCAST;
		return;
	}
	if (match_string_ident(attribute, "type")) {
		if (expr->type == EXPR_COMMA) {
			int mask = get_expression_value(expr->left);
			int value = get_expression_value(expr->right);
			if (value & ~mask) {
				warn(expr->pos, "nonsense attribute types");
				return;
			}
			ctype->typemask |= mask;
			ctype->type |= value;
			return;
		}
	}
	fprintf(stderr, "saw attribute '%s'\n", show_ident(attribute));
}


struct token *attribute_specifier(struct token *token, struct ctype *ctype)
{
	token = expect(token, '(', "after attribute");
	token = expect(token, '(', "after attribute");

	for (;;) {
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
		handle_attribute(ctype, attribute_name, attribute_expr);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}

	token = expect(token, ')', "after attribute");
	token = expect(token, ')', "after attribute");
	return token;
}

#define MOD_SPECIALBITS (MOD_STRUCTOF | MOD_UNIONOF | MOD_ENUMOF | MOD_ATTRIBUTE | MOD_TYPEOF)

static struct token *type_qualifiers(struct token *next, struct ctype *ctype)
{
	struct token *token;
	while ( (token = next) != NULL ) {
		struct symbol *s, *base_type;
		unsigned long mod;

		next = token->next;
		if (token_type(token) != TOKEN_IDENT) 
			break;
		s = lookup_symbol(token->ident, NS_TYPEDEF);
		if (!s)
			break;
		mod = s->ctype.modifiers;
		base_type = s->ctype.base_type;
		if (base_type)
			break;
		if (mod & ~(MOD_CONST | MOD_VOLATILE))
			break;
		ctype->modifiers |= mod;
	}
	return token;
}

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

static struct token *declaration_specifiers(struct token *next, struct ctype *ctype)
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
			if (type != ctype->base_type) {
				if (ctype->base_type) {
					warn(token->pos, "Strange mix of types");
					continue;
				}
				ctype->base_type = type;
			}
		}
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
				warn(token->pos, "Just how %s do you want this type to be?",
					modifier_string(dup));
			ctype->modifiers = old | mod | extra;
		}
		if ((ctype->type ^ thistype.type) & (ctype->typemask & thistype.typemask)) {
			warn(token->pos, "inconsistend attribute types");
			thistype.type = 0;
			thistype.typemask = 0;
		}
		ctype->type |= thistype.type;
		ctype->typemask |= thistype.typemask;
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
			struct ctype ctype = { 0, };
			token = attribute_specifier(token->next, &ctype);
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
		ptr->ctype.base_type = base_type;

		base_type = ptr;
		ctype->modifiers = modifiers & MOD_STORAGE;
		ctype->base_type = base_type;

		token = type_qualifiers(token->next, ctype);
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
	
		token = declaration_specifiers(token, &ctype);
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

	token = declaration_specifiers(token, &ctype);
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
	token = declaration_specifiers(token, &sym->ctype);
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

struct token *statement(struct token *token, struct statement **tree)
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
			struct expression *e1, *e2, *e3;
			struct statement *iterator;

			token = expect(token->next, '(', "after 'for'");
			token = parse_expression(token, &e1);
			token = expect(token, ';', "in 'for'");
			token = parse_expression(token, &e2);
			token = expect(token, ';', "in 'for'");
			token = parse_expression(token, &e3);
			token = expect(token, ')', "in 'for'");
			token = statement(token, &iterator);

			stmt->type = STMT_ITERATOR;
			stmt->iterator_pre_statement = make_statement(e1);
			stmt->iterator_pre_condition = e2;
			stmt->iterator_post_statement = make_statement(e3);
			stmt->iterator_post_condition = e2;
			stmt->iterator_statement = iterator;

			return token;
		}
		if (token->ident == &while_ident) {
			struct expression *expr;
			struct statement *iterator;

			token = parens_expression(token->next, &expr, "after 'while'");
			token = statement(token, &iterator);

			stmt->type = STMT_ITERATOR;
			stmt->iterator_pre_condition = expr;
			stmt->iterator_post_condition = expr;
			stmt->iterator_statement = iterator;

			return token;
		}
		if (token->ident == &do_ident) {
			struct expression *expr;
			struct statement *iterator;
			
			token = statement(token->next, &iterator);
			if (token_type(token) == TOKEN_IDENT && token->ident == &while_ident)
				token = token->next;
			else
				warn(token->pos, "expected 'while' after 'do'");
			token = parens_expression(token, &expr, "after 'do-while'");

			stmt->type = STMT_ITERATOR;
			stmt->iterator_post_condition = expr;
			stmt->iterator_statement = iterator;

			return expect(token, ';', "after statement");
		}
		if (token->ident == &goto_ident) {
			stmt->type = STMT_GOTO;
			token = token->next;			
			if (token_type(token) == TOKEN_IDENT) {
				stmt->goto_label = label_symbol(token);
				token = token->next;
			} else
				warn(token->pos, "invalid label");
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
		if (idx_to < idx_from)
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

static void declare_argument(struct symbol *sym, void *data, int flags)
{
	struct symbol *decl = data;

	if (!sym->ident) {
		warn(decl->pos, "no identifier for function argument");
		return;
	}
	bind_symbol(sym, sym->ident, NS_SYMBOL);
}

static struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct ident *ident = NULL;
	struct symbol *decl;
	struct ctype ctype = { 0, };
	struct symbol *base_type;

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype);
	decl = alloc_symbol(token->pos, SYM_NODE);
	decl->ctype = ctype;
	token = pointer(token, &decl->ctype);
	token = declarator(token, &decl, &ident);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	decl->ident = ident;

	/* type define declaration? */
	if (ctype.modifiers & MOD_TYPEDEF) {
		bind_symbol(decl, ident, NS_TYPEDEF);
	} else {
		add_symbol(list, decl);
		bind_symbol(decl, ident, NS_SYMBOL);
	}

	base_type = decl->ctype.base_type;
	if (base_type && base_type->type == SYM_FN && match_op(token, '{')) {
		base_type->stmt = alloc_statement(token->pos, STMT_COMPOUND);
		start_function_scope();
		symbol_iterate(base_type->arguments, declare_argument, decl);
		token = compound_statement(token->next, base_type->stmt);
		end_function_scope();
		return expect(token, '}', "at end of function");
	}

	for (;;) {
		if (match_op(token, '='))
			token = initializer(&decl->initializer, token->next);
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

		if (ctype.modifiers & MOD_TYPEDEF) {
			bind_symbol(decl, ident, NS_TYPEDEF);
		} else {
			add_symbol(list, decl);
			bind_symbol(decl, ident, NS_SYMBOL);
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
