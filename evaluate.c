/*
 * sparse/evaluate.c
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 *
 * Evaluate constant expressions.
 */
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include "lib.h"
#include "parse.h"
#include "token.h"
#include "symbol.h"
#include "target.h"
#include "expression.h"

static int evaluate_symbol(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		warn(expr->token, "undefined identifier '%s'", show_token(expr->token));
		return 0;
	}
	examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return 0;

	expr->ctype = base_type;
	examine_symbol_type(base_type);

	/* enum's can be turned into plain values */
	if (base_type->type == SYM_ENUM) {
		expr->type = EXPR_VALUE;
		expr->value = sym->value;
	}
	return 1;
}

static int get_int_value(struct expression *expr, const char *str)
{
	unsigned long long value = 0;
	unsigned int base = 10, digit;

	switch (str[0]) {
	case 'x':
		base = 18;	// the -= 2 for the octal case will
		str++;		// skip the 'x'
	/* fallthrough */
	case 'o':
		str++;		// skip the 'o' or 'x/X'
		base -= 2;	// the fall-through will make this 8
	}
	while ((digit = hexval(*str)) < base) {
		value = value * base + digit;
		str++;
	}
	expr->type = EXPR_VALUE;
	expr->ctype = &int_ctype;
	expr->value = value;
	return 1;
}

static int evaluate_constant(struct expression *expr)
{
	struct token *token = expr->token;

	switch (token->type) {
	case TOKEN_INTEGER:
		return get_int_value(expr, token->integer);

	case TOKEN_CHAR:
		expr->type = EXPR_VALUE;
		expr->ctype = &int_ctype;
		expr->value = (char) token->character;
		return 1;
	case TOKEN_STRING:
		expr->ctype = &string_ctype;
		return 1;
	default:
		warn(token, "non-typed expression");
	}
	return 0;
}

static struct symbol *bigger_int_type(struct symbol *left, struct symbol *right)
{
	unsigned long lmod, rmod, mod;

	if (left == right)
		return left;

	if (left->bit_size > right->bit_size)
		return left;

	if (right->bit_size > left->bit_size)
		return right;

	/* Same size integers - promote to unsigned, promote to long */
	lmod = left->ctype.modifiers;
	rmod = right->ctype.modifiers;
	mod = lmod | rmod;
	if (mod == lmod)
		return left;
	if (mod == rmod)
		return right;
	return ctype_integer(mod);
}

static struct expression * promote(struct expression *old, struct symbol *type)
{
	struct expression *expr = alloc_expression(old->token, EXPR_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	return expr;
}

static int is_ptr_type(struct symbol *type)
{
	return type->type == SYM_PTR || type->type == SYM_ARRAY;
}

static int is_int_type(struct symbol *type)
{
	return type->ctype.base_type == &int_type;
}

static struct symbol * compatible_binop(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	/* Pointer types? */
	if (is_ptr_type(ltype) || is_ptr_type(rtype)) {
		/* NULL pointer? */
		if (left->type == EXPR_VALUE && !left->value)
			return &ptr_ctype;
		if (right->type == EXPR_VALUE && !right->value)
			return &ptr_ctype;

		// FIXME!!
		return ltype;
	}

	/* Integer promotion? */
	if (ltype->type == SYM_ENUM)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM)
		rtype = &int_ctype;
	if (is_int_type(ltype) && is_int_type(rtype)) {
		struct symbol *ctype = bigger_int_type(ltype, rtype);

		/* Don't bother promoting same-size entities, it only adds clutter */
		if (ltype->bit_size != ctype->bit_size)
			expr->left = promote(left, ctype);
		if (rtype->bit_size != ctype->bit_size)
			expr->right = promote(right, ctype);
		return ctype;
	}

	warn(expr->token, "incompatible types for operation");
	return NULL;
}

static int evaluate_binop(struct expression *expr)
{
	struct symbol *ctype;
	// FIXME! handle int + ptr and ptr - ptr here

	ctype = compatible_binop(expr);
	if (!ctype)
		return 0;
	expr->ctype = ctype;
	return 1;
}

static int evaluate_comma(struct expression *expr)
{
	expr->ctype = expr->right->ctype;
	return 1;
}

static int evaluate_compare(struct expression *expr)
{
	if (!compatible_binop(expr))
		return 0;
	expr->ctype = &bool_ctype;
	return 1;
}

static int evaluate_assignment(struct expression *expr)
{
	// FIXME! We need to cast and check the rigth side!
	expr->ctype = expr->left->ctype;
	return 1;
}

static int evaluate_preop(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;

	switch (expr->op) {
	case '(':
		expr->ctype = ctype;
		return 1;

	case '*':
		if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
			warn(expr->token, "cannot derefence this type");
			return 0;
		}
		examine_symbol_type(expr->ctype);
		expr->ctype = ctype->ctype.base_type;
		if (!expr->ctype) {
			warn(expr->token, "undefined type");
			return 0;
		}
		return 1;

	case '&': {
		struct symbol *symbol = alloc_symbol(expr->token, SYM_PTR);
		symbol->ctype.base_type = ctype;
		symbol->bit_size = BITS_IN_POINTER;
		symbol->alignment = POINTER_ALIGNMENT;
		expr->ctype = symbol;
		return 1;
	}

	case '!':
		expr->ctype = &bool_ctype;
		return 1;

	default:
		expr->ctype = ctype;
		return 1;
	}
}

static int evaluate_postop(struct expression *expr)
{
	return 0;
}

struct symbol *find_identifier(struct ident *ident, struct symbol_list *_list, int *offset)
{
	struct ptr_list *head = (struct ptr_list *)_list;
	struct ptr_list *list = head;

	if (!head)
		return NULL;
	do {
		int i;
		for (i = 0; i < list->nr; i++) {
			struct symbol *sym = (struct symbol *) list->list[i];
			if (sym->ident) {
				if (sym->ident->ident != ident)
					continue;
				*offset = sym->offset;
				return sym;
			} else {
				struct symbol *ctype = sym->ctype.base_type;
				struct symbol *sub;
				if (!ctype)
					continue;
				if (ctype->type != SYM_UNION && ctype->type != SYM_STRUCT)
					continue;
				sub = find_identifier(ident, ctype->symbol_list, offset);
				if (!sub)
					continue;
				*offset += sym->offset;
				return sub;
			}	
		}
	} while ((list = list->next) != head);
	return NULL;
}

/* structure/union dereference */
static int evaluate_dereference(struct expression *expr)
{
	int offset;
	struct symbol *ctype, *member;
	struct expression *deref = expr->deref, *add;
	struct token *token = expr->member;

	if (!evaluate_expression(deref) || !token)
		return 0;

	ctype = deref->ctype;
	if (expr->op == SPECIAL_DEREFERENCE) {
		if (ctype->type != SYM_PTR) {
			warn(expr->token, "expected a pointer to a struct/union");
			return 0;
		}
		ctype = ctype->ctype.base_type;
	}
	if (!ctype || (ctype->type != SYM_STRUCT && ctype->type != SYM_UNION)) {
		warn(expr->token, "expected structure or union");
		return 0;
	}
	offset = 0;
	member = find_identifier(token->ident, ctype->symbol_list, &offset);
	if (!member) {
		warn(expr->token, "no such struct/union member");
		return 0;
	}

	add = deref;
	if (offset != 0) {
		add = alloc_expression(expr->token, EXPR_BINOP);
		add->op = '+';
		add->ctype = &ptr_ctype;
		add->left = deref;
		add->right = alloc_expression(expr->token, EXPR_VALUE);
		add->right->ctype = &int_ctype;
		add->right->value = offset;
	}

	ctype = member->ctype.base_type;
	if (ctype->type == SYM_BITFIELD) {
		expr->type = EXPR_BITFIELD;
		expr->ctype = ctype->ctype.base_type;
		expr->bitpos = member->bit_offset;
		expr->nrbits = member->fieldwidth;
		expr->address = add;
	} else {
		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->ctype = ctype;
		expr->unop = add;
	}

	return 1;
}

static int evaluate_sizeof(struct expression *expr)
{
	int size;

	if (expr->cast_type) {
		examine_symbol_type(expr->cast_type);
		size = expr->cast_type->bit_size;
	} else {
		if (!evaluate_expression(expr->cast_expression))
			return 0;
		size = expr->cast_expression->ctype->bit_size;
	}
	if (size & 7) {
		warn(expr->token, "cannot size expression");
		return 0;
	}
	expr->type = EXPR_VALUE;
	expr->value = size >> 3;
	expr->ctype = size_t_ctype;
	return 1;
}

static int evaluate_lvalue_expression(struct expression *expr)
{
	// FIXME!
	return evaluate_expression(expr);
}

static int evaluate_expression_list(struct expression_list *head)
{
	if (head) {
		struct ptr_list *list = (struct ptr_list *)head;
		do {
			int i;
			for (i = 0; i < list->nr; i++) {
				struct expression *expr = (struct expression *)list->list[i];
				evaluate_expression(expr);
			}
		} while ((list = list->next) != (struct ptr_list *)head);
	}
	// FIXME!
	return 1;
}

static int evaluate_call(struct expression *expr)
{
	int args, fnargs;
	struct symbol *ctype;
	struct expression *fn = expr->fn;
	struct expression_list *arglist = expr->args;

	if (!evaluate_expression(fn))
		return 0;
	if (!evaluate_expression_list(arglist))
		return 0;
	ctype = fn->ctype;
	if (ctype->type != SYM_FN) {
		warn(expr->token, "not a function");
		return 0;
	}
	args = expression_list_size(expr->args);
	fnargs = symbol_list_size(ctype->arguments);
	if (args < fnargs)
		warn(expr->token, "not enough arguments for function");
	if (args > fnargs && !ctype->variadic)
		warn(expr->token, "too many arguments for function");
	expr->ctype = ctype->ctype.base_type;
	return 1;
}

int evaluate_expression(struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->ctype)
		return 1;

	switch (expr->type) {
	case EXPR_CONSTANT:
		return evaluate_constant(expr);
	case EXPR_SYMBOL:
		return evaluate_symbol(expr);
	case EXPR_BINOP:
		if (!evaluate_expression(expr->left))
			return 0;
		if (!evaluate_expression(expr->right))
			return 0;
		return evaluate_binop(expr);
	case EXPR_COMMA:
		if (!evaluate_expression(expr->left))
			return 0;
		if (!evaluate_expression(expr->right))
			return 0;
		return evaluate_comma(expr);
	case EXPR_COMPARE:
		if (!evaluate_expression(expr->left))
			return 0;
		if (!evaluate_expression(expr->right))
			return 0;
		return evaluate_compare(expr);
	case EXPR_ASSIGNMENT:
		if (!evaluate_lvalue_expression(expr->left))
			return 0;
		if (!evaluate_expression(expr->right))
			return 0;
		return evaluate_assignment(expr);
	case EXPR_PREOP:
		if (!evaluate_expression(expr->unop))
			return 0;
		return evaluate_preop(expr);
	case EXPR_POSTOP:
		if (!evaluate_expression(expr->unop))
			return 0;
		return evaluate_postop(expr);
	case EXPR_CAST:
		if (!evaluate_expression(expr->cast_expression))
			return 0;
		expr->ctype = expr->cast_type;
		return 1;
	case EXPR_SIZEOF:
		return evaluate_sizeof(expr);
	case EXPR_DEREF:
		return evaluate_dereference(expr);
	case EXPR_CALL:
		return evaluate_call(expr);
	default:
		break;
	}
	return 0;
}

long long get_expression_value(struct expression *expr)
{
	long long left, middle, right;

	switch (expr->type) {
	case EXPR_SIZEOF:
		if (expr->cast_type) {
			examine_symbol_type(expr->cast_type);
			if (expr->cast_type->bit_size & 7) {
				warn(expr->token, "type has no size");
				return 0;
			}
			return expr->cast_type->bit_size >> 3;
		}
		warn(expr->token, "expression sizes not yet supported");
		return 0;
	case EXPR_CONSTANT:
		evaluate_constant(expr);
		if (expr->type == EXPR_VALUE)
			return expr->value;
		return 0;
	case EXPR_SYMBOL: {
		struct symbol *sym = expr->symbol;
		if (!sym || !sym->ctype.base_type || sym->ctype.base_type->type != SYM_ENUM) {
			warn(expr->token, "undefined identifier in constant expression");
			return 0;
		}
		return sym->value;
	}

#define OP(x,y)	case x: return left y right;
	case EXPR_BINOP:
	case EXPR_COMPARE:
		left = get_expression_value(expr->left);
		if (!left && expr->op == SPECIAL_LOGICAL_AND)
			return 0;
		if (left && expr->op == SPECIAL_LOGICAL_OR)
			return 1;
		right = get_expression_value(expr->right);
		switch (expr->op) {
			OP('+',+); OP('-',-); OP('*',*); OP('/',/);
			OP('%',%); OP('<',<); OP('>',>);
			OP('&',&);OP('|',|);OP('^',^);
			OP(SPECIAL_EQUAL,==); OP(SPECIAL_NOTEQUAL,!=);
			OP(SPECIAL_LTE,<=); OP(SPECIAL_LEFTSHIFT,<<);
			OP(SPECIAL_RIGHTSHIFT,>>); OP(SPECIAL_GTE,>=);
			OP(SPECIAL_LOGICAL_AND,&&);OP(SPECIAL_LOGICAL_OR,||);
		}
		break;

#undef OP
#define OP(x,y)	case x: return y left;
	case EXPR_PREOP:
		left = get_expression_value(expr->unop);
		switch (expr->op) {
			OP('+', +); OP('-', -); OP('!', !); OP('~', ~); OP('(', );
		}
		break;

	case EXPR_CONDITIONAL:
		left = get_expression_value(expr->conditional);
		if (!expr->cond_true)
			middle = left;
		else
			middle = get_expression_value(expr->cond_true);
		right = get_expression_value(expr->cond_false);
		return left ? middle : right;

	default:
		break;
	}
	error(expr->token, "bad constant expression");
	return 0;
}
