/*
 * Stupid C parser, version 1e-6.
 *
 * Let's see how hard this is to do.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "token.h"
#include "parse.h"
#include "symbol.h"

void show_expression(struct expression *expr)
{
	if (!expr)
		return;

	printf("< ");
	switch (expr->type) {
	case EXPR_BINOP:
		show_expression(expr->left);
		printf(" %s ", show_special(expr->op));
		show_expression(expr->right);
		break;
	case EXPR_PREOP:
		printf(" %s ", show_special(expr->op));
		show_expression(expr->unop);
		break;
	case EXPR_POSTOP:
		show_expression(expr->unop);
		printf(" %s ", show_special(expr->op));
		break;
	case EXPR_PRIMARY:
		printf("%s", show_token(expr->token));
		break;
	case EXPR_DEREF:
		show_expression(expr->deref);
		printf("%s", show_special(expr->op));
		printf("%s", show_token(expr->member));
		break;
	case EXPR_CAST:
		printf("(");
		show_type(expr->cast_type);
		printf(")");
		show_expression(expr->cast_expression);
		break;
	default:
		printf("WTF");
	}
	printf(" >");
}

static struct expression *alloc_expression(struct token *token, int type)
{
	struct expression *expr = malloc(sizeof(struct expression));

	if (!expr)
		die("Unable to allocate expression");
	memset(expr, 0, sizeof(*expr));
	expr->type = type;
	expr->token = token;
	return expr;
}

static int match_op(struct token *token, int op)
{
	return token && token->type == TOKEN_SPECIAL && token->special == op;
}

static struct token *skip_to(struct token *token, int op)
{
	while (token && !match_op(token, op))
		token = token->next;
	return token;
}

static struct token *expect(struct token *token, int op, const char *where)
{
	if (!match_op(token, op)) {
		warn(token, "Expected %s %s", show_special(op), where);
		warn(token, "got %s", show_token(token));
		if (op == ';')
			token = skip_to(token, op);
		return token;
	}
	return token->next;
}

static struct token *comma_expression(struct token *, struct expression **);

static struct token *primary_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	if (!token) {
		*tree = NULL;
		return token;
	}

	switch (token->type) {
	case TOKEN_IDENT:
	case TOKEN_INTEGER:
	case TOKEN_FP:
	case TOKEN_STRING:
		expr = alloc_expression(token, EXPR_PRIMARY);
		token = token->next;
		break;

	case TOKEN_SPECIAL:
		if (token->special == '(') {
			expr = alloc_expression(token, EXPR_PREOP);
			expr->op = '(';
			token = parse_expression(token->next, &expr->unop);
			token = expect(token, ')', "in expression");
			break;
		}
	default:
		;
	}
	*tree = expr;
	return token;
}

static struct token *postfix_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	token = primary_expression(token, &expr);
	while (expr && token && token->type == TOKEN_SPECIAL) {
		switch (token->special) {
		case '[': {			/* Array dereference */
			struct expression *array_expr = alloc_expression(token, EXPR_BINOP);
			array_expr->op = '[';
			array_expr->left = expr;
			token = parse_expression(token->next, &array_expr->right);
			token = expect(token, ']', "at end of array dereference");
			expr = array_expr;
			continue;
		}
		case SPECIAL_INCREMENT:		/* Post-increment */
		case SPECIAL_DECREMENT:	{	/* Post-decrement */
			struct expression *post = alloc_expression(token, EXPR_POSTOP);
			post->op = token->special;
			post->unop = expr;
			expr = post;
			token = token->next;
			continue;
		}
		case '.':			/* Structure member dereference */
		case SPECIAL_DEREFERENCE: {	/* Structure pointer member dereference */
			struct expression *deref = alloc_expression(token, EXPR_DEREF);
			deref->op = token->special;
			deref->deref = expr;
			token = token->next;
			if (!token || token->type != TOKEN_IDENT) {
				warn(token, "Expected member name");
				break;
			}
			deref->member = token;
			token = token->next;
			expr = deref;
			continue;
		}

		case '(': {			/* Function call */
			struct expression *call = alloc_expression(token, EXPR_BINOP);
			call->op = '(';
			call->left = expr;
			token = comma_expression(token->next, &call->right);
			token = expect(token, ')', "in function call");
			expr = call;
			continue;
		}

		default:
			break;
		}
		break;
	}
	*tree = expr;
	return token;
}

static struct token *unary_expression(struct token *token, struct expression **tree)
{
	return postfix_expression(token, tree);
}

static struct token *typename(struct token *, struct symbol **);

/*
 * Ambiguity: a '(' can be either a cast-expression or
 * a primary-expression depending on whether it is followed
 * by a type or not. 
 */
static struct token *cast_expression(struct token *token, struct expression **tree)
{
	if (match_op(token, '(')) {
		struct token *next = token->next;
		if (next && next->type == TOKEN_IDENT) {
			struct symbol *sym = lookup_symbol(next->ident, NS_TYPEDEF);
			if (sym) {
				struct expression *cast = alloc_expression(next, EXPR_CAST);
				token = typename(next, &cast->cast_type);
				token = expect(token, ')', "at end of cast operator");
				token = cast_expression(token, &cast->cast_expression);
				*tree = cast;
				return token;
			}
		}
	}
	return unary_expression(token, tree);
}

/* Generic left-to-right binop parsing */
static struct token *lr_binop_expression(struct token *token, struct expression **tree,
	struct token *(*inner)(struct token *, struct expression **), ...)
{
	struct expression *left = NULL;
	struct token * next = inner(token, &left);

	if (left) {
		while (next && next->type == TOKEN_SPECIAL) {
			struct expression *top, *right = NULL;
			int op = next->special;
			va_list args;

			va_start(args, inner);
			for (;;) {
				int nextop = va_arg(args, int);
				if (!nextop)
					goto out;
				if (op == nextop)
					break;
			}
			va_end(args);
			top = alloc_expression(next, EXPR_BINOP);
			next = inner(next->next, &right);
			if (!right) {
				warn(token, "Syntax error");
				break;
			}
			top->op = op;
			top->left = left;
			top->right = right;
			left = top;
		}
	}
out:
	*tree = left;
	return next;
}

static struct token *multiplicative_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, cast_expression, '*', '/', '%', 0);
}

static struct token *additive_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, multiplicative_expression, '+', '-', 0);
}

static struct token *shift_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, additive_expression, SPECIAL_LEFTSHIFT, SPECIAL_RIGHTSHIFT, 0);
}

static struct token *relational_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, shift_expression, '<', '>', SPECIAL_LTE, SPECIAL_GTE, 0);
}

static struct token *equality_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, relational_expression, SPECIAL_EQUAL, SPECIAL_NOTEQUAL, 0);
}

static struct token *bitwise_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, equality_expression, '&', 0);
}

static struct token *bitwise_xor_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_and_expression, '^', 0);
}

static struct token *bitwise_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_xor_expression, '|', 0);
}

static struct token *logical_and_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, bitwise_or_expression, SPECIAL_LOGICAL_AND, 0);
}

static struct token *logical_or_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, logical_and_expression, SPECIAL_LOGICAL_OR, 0);
}

struct token *assignment_expression(struct token *token, struct expression **tree)
{
	struct expression *left = NULL;

	token = logical_or_expression(token, &left);
	if (token && token->type == TOKEN_SPECIAL) {
		static const int assignments[] = {
			'=', SPECIAL_ADD_ASSIGN, SPECIAL_MINUS_ASSIGN,
			SPECIAL_TIMES_ASSIGN, SPECIAL_DIV_ASSIGN,
			SPECIAL_MOD_ASSIGN, SPECIAL_SHL_ASSIGN,
			SPECIAL_SHR_ASSIGN, SPECIAL_AND_ASSIGN,
			SPECIAL_OR_ASSIGN, SPECIAL_XOR_ASSIGN };
		int i, op = token->special;
		for (i = 0; i < sizeof(assignments)/sizeof(int); i++)
			if (assignments[i] == op) {
				struct expression * expr = alloc_expression(token, EXPR_BINOP);
				expr->left = left;
				expr->op = op;
				*tree = expr;
				return assignment_expression(token->next, &expr->right);
			}
	}
	*tree = left;
	return token;
}

struct token *comma_expression(struct token *token, struct expression **tree)
{
	return lr_binop_expression(token, tree, assignment_expression, ',', 0);
}

struct token *parse_expression(struct token *token, struct expression **tree)
{
	return comma_expression(token,tree);
}

struct statement *alloc_statement(struct token * token, int type)
{
	struct statement *stmt = malloc(sizeof(*stmt));
	if (!stmt)
		die("Out of memory for statements");
	memset(stmt, 0, sizeof(*stmt));
	stmt->type = type;
	return stmt;
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list);

static void force_default_type(struct symbol *sym)
{
	if (!sym->base_type)
		sym->base_type = &int_type;
}

static struct symbol *indirect(struct symbol *parent, int type)
{
	struct symbol *sym = alloc_symbol(parent->token, type);

	force_default_type(parent);
	sym->base_type = parent;
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
	struct token *token, struct symbol **p,
	struct token *(*parse)(struct token *, struct symbol *))
{
	struct symbol *sym;

	if (token->type == TOKEN_IDENT) {
		sym = lookup_or_create_symbol(ns, type, token);
		token = token->next;
		*p = indirect(sym, type);
		if (match_op(token, '{')) {
			token = parse(token, sym);
			token = expect(token, '}', "end of struct-union-enum-specifier");
		}
		return token;
	}

	// private struct/union/enum type
	if (!match_op(token, '{')) {
		warn(token, "expected declaration");
		*p = &bad_type;
		return token;
	}

	sym = alloc_symbol(token, type);
	*p = indirect(sym, type);
	token = parse(token->next, sym);
	return expect(token, '}', "end of specifier");
}

static struct token *parse_struct_declaration(struct token *token, struct symbol *sym)
{
	return struct_declaration_list(token->next, &sym->symbol_list);
}

struct token *struct_or_union_specifier(enum type type, struct token *token, struct symbol **p)
{
	return struct_union_enum_specifier(NS_STRUCT, type, token, p, parse_struct_declaration);
}

/* FIXME! */
static struct token *parse_enum_declaration(struct token *token, struct symbol *sym)
{
	return token;
}

struct token *enum_specifier(struct token *token, struct symbol **p)
{
	return struct_union_enum_specifier(NS_ENUM, SYM_ENUM, token, p, parse_enum_declaration);
}

static struct token *declaration_specifiers(struct token *next, struct symbol *sym)
{
	struct token *token;

	while ( (token = next) != NULL ) {
		struct ident *ident;
		struct symbol *s;
		struct symbol *type;
		unsigned long mod;

		next = token->next;
		if (token->type != TOKEN_IDENT)
			break;
		ident = token->ident;

		s = lookup_symbol(ident, NS_TYPEDEF);
		if (!s) {
			if (ident == &struct_ident)
				next = struct_or_union_specifier(SYM_STRUCT, next, &s);
			else if (ident == &union_ident)
				next = struct_or_union_specifier(SYM_UNION, next, &s);
			else if (ident == &enum_ident)
				next = enum_specifier(next, &s);
			if (!s)
				break;
		}
		type = s->base_type;
		mod = s->modifiers;
		if (type) {
			if (type != sym->base_type) {
				if (sym->base_type) {
					warn(token, "Strange mix of types");
					continue;
				}
				sym->base_type = type;
			}
		}
		if (mod) {
			unsigned long old = sym->modifiers;
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
			sym->modifiers = old | mod | extra;
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

static struct token *abstract_function_declarator(struct token *token, struct symbol **tree);

static struct token *direct_declarator(struct token *token, struct symbol **tree,
	struct token *(*declarator)(struct token *, struct symbol **, struct token **),
	struct token **p)
{
	if (p && token && token->type == TOKEN_IDENT) {
		if (lookup_symbol(token->ident, NS_TYPEDEF)) {
			warn(token, "unexpected type/qualifier");
			return token;
		}
		*p = token;
		token = token->next;
	}

	for (;;) {
		if (!token || token->type != TOKEN_SPECIAL)
			return token;

		/*
		 * This can be either a function or a grouping!
		 * A grouping must start with '*', '[' or '('..
		 */
		if (token->special == '(') {
			struct token *next = token->next;
			if (!p && next && next->type == TOKEN_SPECIAL) {
				if (next->special == '*' ||
				    next->special == '(' ||
				    next->special == '[') {
					token = declarator(next, tree, p);
					token = expect(token, ')', "in nested declarator");
					continue;
				    }
			}
			*tree = indirect(*tree, SYM_FN);
			token = abstract_function_declarator(next, &(*tree)->children);
			token = expect(token, ')', "in function declarator");
			continue;
		}
		if (token->special == '[') {
			*tree = indirect(*tree, SYM_ARRAY);
			token = abstract_array_declarator(token->next, tree);
			token = expect(token, ']', "in abstract_array_declarator");
			continue;
		}
		break;
	}
	(*tree)->token = *p;
	return token;
}

static struct token *pointer(struct token *token, struct symbol **tree)
{
	while (match_op(token,'*')) {
		*tree = indirect(*tree, SYM_PTR);
		token = declaration_specifiers(token->next, *tree);
	}
	return token;
}

static struct token *generic_declarator(struct token *token, struct symbol **tree, struct token **p)
{
	token = pointer(token, tree);
	return direct_declarator(token, tree, generic_declarator, p);
}

static struct token *struct_declaration_list(struct token *token, struct symbol_list **list)
{
	for (;;) {
		struct symbol *spec = alloc_symbol(token, SYM_TYPE);
	
		token = declaration_specifiers(token, spec);
		for (;;) {
			struct symbol *declaration = spec;
			struct token *ident = NULL;
			token = pointer(token, &declaration);
			token = direct_declarator(token, &declaration, generic_declarator, &ident);
			if (ident) {
				printf("named structure declarator %s:\n  ", show_token(ident));
				show_type(declaration);
				printf("\n\n");
			}
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

#define abstract_declarator(token, symbol) \
	generic_declarator(token, symbol, NULL)

static struct token *parameter_declaration(struct token *token, struct symbol **tree)
{
	struct token *ident = NULL;

	*tree = alloc_symbol(token, SYM_TYPE);
	token = declaration_specifiers(token, *tree);
	token = pointer(token, tree);
	token = direct_declarator(token, tree, generic_declarator, &ident);
	if (ident) {
		printf("named parameter declarator %s:\n  ", show_token(ident));
		show_type(*tree);
		printf("\n\n");
	}
	return token;
}

static struct token *typename(struct token *token, struct symbol **tree)
{
	*tree = alloc_symbol(token, SYM_TYPE);
	token = declaration_specifiers(token, *tree);
	return abstract_declarator(token, tree);

}

struct token *statement(struct token *token, struct statement **tree)
{
	struct statement *stmt;
	struct expression *expr;

	token = parse_expression(token, &expr);
	if (!expr) {
		*tree = NULL;
		return token;
	}
	token = expect(token, ';', "at end of statement");
	stmt = alloc_statement(token, STMT_EXPRESSION);
	stmt->expression = expr;
	*tree = stmt;
	return token;
}

struct token * statement_list(struct token *token, struct statement **tree)
{
	struct statement *stmt;

	do {
		stmt = NULL;
		token = statement(token, &stmt);
		*tree = stmt;
		tree = &stmt->next;
	} while (stmt);
	return token;
}

static struct token *parameter_type_list(struct token *token, struct symbol **tree)
{
	for (;;) {
		struct symbol *sym = alloc_symbol(token, SYM_TYPE);

		*tree = sym;
		token = parameter_declaration(token, tree);
		if (!match_op(token, ','))
			break;
		if (*tree)
			tree = &(*tree)->next;
		token = token->next;
	}
	return token;
}

static struct token *abstract_function_declarator(struct token *token, struct symbol **tree)
{
	return parameter_type_list(token, tree);
}

static struct token *compound_statement(struct token *token, struct statement **tree)
{
	return statement_list(token, tree);
}

static struct token *initializer(struct token *token, struct symbol *sym);
static struct token *initializer_list(struct token *token, struct symbol *sym)
{
	for (;;) {
		token = initializer(token, sym);
		if (!match_op(token, ','))
			break;
		token = token->next;
	}
	return token;
}

static struct token *initializer(struct token *token, struct symbol *sym)
{
	struct expression *expr;
	if (match_op(token, '{')) {
		token = initializer_list(token, sym);
		return expect(token, '}', "at end of initializer");
	}
	return assignment_expression(token, &expr);
}

static struct token *external_declaration(struct token *token, struct symbol_list **list)
{
	struct token *ident = NULL;
	struct symbol *specifiers;
	struct symbol *declarator;

	/* Parse declaration-specifiers, if any */
	specifiers = alloc_symbol(token, SYM_TYPE);
	token = declaration_specifiers(token, specifiers);

	declarator = specifiers;
	token = generic_declarator(token, &declarator, &ident);

	if (specifiers->modifiers & SYM_TYPEDEF) {
		specifiers->modifiers &= ~SYM_TYPEDEF;
		if (ident)
			bind_symbol(indirect(declarator, SYM_TYPEDEF), ident->ident, NS_TYPEDEF);
			
		return expect(token, ';', "end of typedef");
	}

	add_symbol(list, declarator);

	if (ident) {
		printf("external_declarator %s:\n  ", show_token(ident));
	}
	show_type(declarator);
	printf("\n\n");

	if (match_op(token, '{')) {
		token = compound_statement(token->next, &declarator->stmt);
		return expect(token, '}', "at end of function");
	}

	for (;;) {
		if (match_op(token, '='))
			token = initializer(token->next, declarator);
		if (!match_op(token, ','))
			break;

		ident = NULL;
		declarator = specifiers;
		token = generic_declarator(token->next, &declarator, &ident);

		add_symbol(list, declarator);

		if (ident) {
			printf("external_declarator %s:\n  ", show_token(ident));
		}
		show_type(declarator);
		printf("\n\n");
	}
	return expect(token, ';', "at end of declaration");
}

void translation_unit(struct token *token, struct symbol_list *list)
{
	while (token)
		token = external_declaration(token, &list);
}
