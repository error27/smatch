#ifndef __GNUC__
typedef int __builtin_va_list;
#endif
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

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "scope.h"

void show_statement(struct statement *stmt)
{
	if (!stmt) {
		printf("\t<nostatement>");
		return;
	}
	switch (stmt->type) {
	case STMT_RETURN:
		printf("\treturn ");
		show_expression(stmt->expression);
		break;
	case STMT_COMPOUND:
		printf("{\n");
		if (stmt->syms) {
			printf("\t");
			show_symbol_list(stmt->syms, "\n\t");
			printf("\n\n");
		}
		show_statement_list(stmt->stmts, ";\n");
		printf("\n}\n\n");
		break;
	case STMT_EXPRESSION:
		printf("\t");
		show_expression(stmt->expression);
		return;
	case STMT_IF:
		printf("\tif (");
		show_expression(stmt->if_conditional);
		printf(")\n");
		show_statement(stmt->if_true);
		if (stmt->if_false) {
			printf("\nelse\n");
			show_statement(stmt->if_false);
		}
		break;
	case STMT_SWITCH:
		printf("\tswitch (");
		show_expression(stmt->switch_expression);
		printf(")\n");
		show_statement(stmt->switch_statement);
		break;

	case STMT_CASE:
		if (!stmt->case_expression)
			printf("default");
		else {
			printf("case ");
			show_expression(stmt->case_expression);
			if (stmt->case_to) {
				printf(" ... ");
				show_expression(stmt->case_to);
			}
		}
		printf(":");
		show_statement(stmt->case_statement);
		break;

	case STMT_BREAK:
		printf("break");
		break;
		
	default:
		printf("WTF");
	}
}

static void show_one_statement(struct statement *stmt, void *sep, int flags)
{
	show_statement(stmt);
	if (!(flags & ITERATE_LAST))
		printf("%s", (const char *)sep);
}

void show_statement_list(struct statement_list *stmt, const char *sep)
{
	statement_iterate(stmt, show_one_statement, (void *)sep);
}

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
		printf("%s<", show_special(expr->op));
		show_expression(expr->unop);
		printf(">");
		break;
	case EXPR_POSTOP:
		show_expression(expr->unop);
		printf(" %s ", show_special(expr->op));
		break;
	case EXPR_CONSTANT:
		printf("%s", show_token(expr->token));
		break;
	case EXPR_SYMBOL:
		if (!expr->symbol) {
			printf("<nosymbol>");
			break;
		}
		printf("<%s:", show_token(expr->symbol->token));
		show_type(expr->symbol);
		printf(">");
		break;
	case EXPR_DEREF:
		show_expression(expr->deref);
		printf("%s", show_special(expr->op));
		printf("%s", show_token(expr->member));
		break;
	case EXPR_CAST:
		printf("<cast>(");
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
	struct expression *expr = __alloc_expression(0);
	expr->type = type;
	expr->token = token;
	return expr;
}

struct statement *alloc_statement(struct token * token, int type)
{
	struct statement *stmt = __alloc_statement(0);
	stmt->type = type;
	return stmt;
}

static int match_oplist(int op, ...)
{
	va_list args;

	va_start(args, op);
	for (;;) {
		int nextop = va_arg(args, int);
		if (!nextop)
			return 0;
		if (op == nextop)
			return 1;
	}
}

int lookup_type(struct token *token)
{
	if (token->type == TOKEN_IDENT)
		return lookup_symbol(token->ident, NS_TYPEDEF) != NULL;
	return 0;
}

static struct token *comma_expression(struct token *, struct expression **);
static struct token *compound_statement(struct token *, struct statement *);
static struct token *initializer(struct token *token, struct ctype *type);

static struct token *parens_expression(struct token *token, struct expression **expr, const char *where)
{
	token = expect(token, '(', where);
	if (match_op(token, '{')) {
		struct expression *e = alloc_expression(token, EXPR_STATEMENT);
		struct statement *stmt = alloc_statement(token, STMT_COMPOUND);
		*expr = e;
		e->statement = stmt;
		start_symbol_scope();
		token = compound_statement(token->next, stmt);
		end_symbol_scope();
		token = expect(token, '}', "at end of statement expression");
	} else
		token = parse_expression(token, expr);
	return expect(token, ')', where);
}

static struct token *primary_expression(struct token *token, struct expression **tree)
{
	struct expression *expr = NULL;

	switch (token->type) {
	case TOKEN_INTEGER:
	case TOKEN_FP:
	case TOKEN_CHAR:
		expr = alloc_expression(token, EXPR_CONSTANT);
		token = token->next;
		break;

	case TOKEN_IDENT: {
		struct symbol *sym = lookup_symbol(token->ident, NS_SYMBOL);
		if (!sym)
			warn(token, "undefined identifier '%s'", token->ident->name);
		expr = alloc_expression(token, EXPR_SYMBOL);
		expr->symbol = sym;
		token = token->next;
		break;
	}

	case TOKEN_STRING:
		expr = alloc_expression(token, EXPR_CONSTANT);
		do {
			token = token->next;
		} while (token->type == TOKEN_STRING);
		break;

	case TOKEN_SPECIAL:
		if (token->special == '(') {
			expr = alloc_expression(token, EXPR_PREOP);
			expr->op = '(';
			token = parens_expression(token, &expr->unop, "in expression");
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
	while (expr && token->type == TOKEN_SPECIAL) {
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
			if (token->type != TOKEN_IDENT) {
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

static struct token *typename(struct token *, struct symbol **);

static struct token *cast_expression(struct token *token, struct expression **tree);
static struct token *unary_expression(struct token *token, struct expression **tree)
{
	if (token->type == TOKEN_IDENT &&
	    (token->ident == &sizeof_ident ||
	     token->ident == &__alignof___ident)) {
		struct expression *sizeof_ex = alloc_expression(token, EXPR_SIZEOF);
		*tree = sizeof_ex;
		tree = &sizeof_ex->unop;
		token = token->next;
		if (!match_op(token, '(') || !lookup_type(token->next))
			return unary_expression(token, &sizeof_ex->cast_expression);
		token = typename(token->next, &sizeof_ex->cast_type);
		return expect(token, ')', "at end of sizeof type-name");
	}

	if (token->type == TOKEN_SPECIAL) {
		if (match_oplist(token->special,
		    SPECIAL_INCREMENT, SPECIAL_DECREMENT,
		    '&', '*', '+', '-', '~', '!', 0)) {
			struct expression *unary = alloc_expression(token, EXPR_PREOP);
			unary->op = token->special;
			*tree = unary;
			return cast_expression(token->next, &unary->unop);
		}
	}
			
	return postfix_expression(token, tree);
}

/*
 * Ambiguity: a '(' can be either a cast-expression or
 * a primary-expression depending on whether it is followed
 * by a type or not. 
 */
static struct token *cast_expression(struct token *token, struct expression **tree)
{
	if (match_op(token, '(')) {
		struct token *next = token->next;
		if (lookup_type(next)) {
			struct expression *cast = alloc_expression(next, EXPR_CAST);

			token = typename(next, &cast->cast_type);
			token = expect(token, ')', "at end of cast operator");
			if (match_op(token, '{'))
				return initializer(token, &cast->cast_type->ctype);
			token = cast_expression(token, &cast->cast_expression);
			*tree = cast;
			return token;
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
		while (next->type == TOKEN_SPECIAL) {
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
				warn(next, "No right hand side of '%s'-expression", show_special(op));
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
	if (token->type == TOKEN_SPECIAL) {
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
		if (op == '?') {
			struct expression *expr = alloc_expression(token, EXPR_CONDITIONAL);
			expr->left = left;
			expr->op = op;
			*tree = expr;
			token = parse_expression(token->next, &expr->cond_true);
			token = expect(token, ':', "in conditional expression");
			return assignment_expression(token, &expr->cond_false);
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

		type = s->ctype.base_type;
		if (type) {
			if (type != ctype->base_type) {
				if (ctype->base_type) {
					warn(token, "Strange mix of types");
					continue;
				}
				ctype->base_type = type;
			}
		}
		if (thistype.modifiers) {
			unsigned long old = ctype->modifiers;
			unsigned long extra = 0, dup;

			if (thistype.modifiers & old & SYM_LONG) {
				extra = SYM_LONGLONG | SYM_LONG;
				thistype.modifiers &= ~SYM_LONG;
				old &= ~SYM_LONG;
			}
			dup = (thistype.modifiers & old) | (extra & old) | (extra & thistype.modifiers);
			if (dup)
				warn(token, "Just how %s do you want this type to be?",
					modifier_string(dup));
			ctype->modifiers = old | thistype.modifiers | extra;
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
			token = parse_expression(token->next, &expr);
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

static struct token *typename(struct token *token, struct symbol **p)
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

static struct token *compound_statement(struct token *token, struct statement *stmt)
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

static struct token *initializer(struct token *token, struct ctype *type)
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
	struct symbol *spec, *decl;
	struct ctype ctype = { 0, };

	/* Parse declaration-specifiers, if any */
	token = declaration_specifiers(token, &ctype);

	spec = alloc_symbol(token, SYM_TYPE);
	spec->ctype = ctype;

	decl = spec;
	token = declarator(token, &decl, &ident);

	/* Just a type declaration? */
	if (!ident)
		return expect(token, ';', "end of type declaration");

	/* type define declaration? */
	if (spec->ctype.modifiers & SYM_TYPEDEF) {
		spec->ctype.modifiers &= ~SYM_TYPEDEF;
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
		decl = spec;
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
