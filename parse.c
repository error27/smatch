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

static struct token *expect(struct token *token, int op, const char *where)
{
	if (!match_op(token, op)) {
		warn(token, "Expected %s %s", show_special(op), where);
		warn(token, "got %s", show_token(token));
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
			struct symbol *sym = next->ident->symbol;
			if (sym && symbol_is_typename(sym)) {
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
	stmt->token = token;
	return stmt;
}

static struct token *declaration_specifiers(struct token *token, struct symbol **tree)
{
	struct symbol *sym = *tree;

	for ( ; token; token = token->next) {
		struct ident *ident;
		struct symbol *s;
		struct symbol *type;
		unsigned long mod;

		/*
		 * Handle 'struct' and 'union' here!
		 */
		if (token->type != TOKEN_IDENT)
			break;
		ident = token->ident;
		s = ident->symbol;
		if (!s || !symbol_is_typename(s))
			break;
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

	if (!sym->base_type)
		sym->base_type = &int_type;
	*tree = sym;
	return token;
}

static struct symbol *indirect(struct symbol *type)
{
	struct symbol *sym = alloc_symbol(SYM_PTR);

	sym->base_type = type;
	return sym;
}

static struct token *parameter_declaration(struct token *token, struct symbol **tree);
static struct token *parameter_type_list(struct token *token, struct symbol **tree)
{
	return parameter_declaration(token, tree);
}

static struct token *abstract_function_declarator(struct token *token, struct symbol **tree)
{
	struct symbol *sym = alloc_symbol(SYM_FN);
	sym->base_type = *tree;
	*tree = sym;
	return parameter_type_list(token, &sym->next_type);
}

static int constant_value(struct expression *expr)
{
	return 0;
}

static struct token *abstract_array_declarator(struct token *token, struct symbol **tree)
{
	struct expression *expr;
	struct symbol *sym = alloc_symbol(SYM_ARRAY);
	sym->base_type = *tree;
	*tree = sym;
	token = parse_expression(token, &expr);
	sym->size = constant_value(expr);
	return token;
}

static struct token *direct_declarator(struct token *token, struct symbol **tree,
	struct token *(*declarator)(struct token *, struct symbol **, struct token **),
	struct token **ident)
{
	if (ident && token->type == TOKEN_IDENT) {
		*ident = token;
		return token->next;
	}
		
	if (token->type != TOKEN_SPECIAL)
		return token;

	/*
	 * This can be either a function or a grouping!
	 * A grouping must start with '*', '[' or '('..
	 */
	if (token->special == '(') {
		struct token *next = token->next;
		if (next && next->type == TOKEN_SPECIAL) {
			if (next->special == '*' ||
			    next->special == '(' ||
			    next->special == '[') {
				token = declarator(next,tree, ident);
				return expect(token, ')', "in nested declarator");
			    }
		}
		token = abstract_function_declarator(next, tree);
		return expect(token, ')', "in function declarator");
	}
	if (token->special == '[') {
		token = abstract_array_declarator(token->next, tree);
		return expect(token, ']', "in abstract_array_declarator");
	}
	return token;
}

static struct token *pointer(struct token *token, struct symbol **tree)
{
	while (match_op(token,'*')) {
		*tree = indirect(*tree);
		token = declaration_specifiers(token->next, tree);
	}
	return token;
}

static struct token *generic_declarator(struct token *token, struct symbol **tree, struct token **ident)
{
	token = pointer(token, tree);
	return direct_declarator(token, tree, generic_declarator, ident);
}

#define abstract_declarator(token, symbol) \
	generic_declarator(token, symbol, NULL)

static struct token *declarator(struct token *token, struct symbol **tree)
{
	struct token *ident = NULL;
	token = pointer(token, tree);
	token = direct_declarator(token, tree, generic_declarator, &ident);
	if (ident) {
		printf("declarator for %s:\n", show_token(ident));
		show_type(*tree);
	}
	return token;
}

static struct token *parameter_declaration(struct token *token, struct symbol **tree)
{
	struct token *ident = NULL;

	*tree = alloc_symbol(SYM_TYPE);
	token = declaration_specifiers(token, tree);
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
	*tree = alloc_symbol(SYM_TYPE);
	token = declaration_specifiers(token, tree);
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
