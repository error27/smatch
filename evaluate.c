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
	unsigned int base = 10, digit, bits;
	unsigned long modifiers, extramod;

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
	modifiers = 0;
	for (;;) {
		char c = *str++;
		if (c == 'u' || c == 'U') {
			modifiers |= MOD_UNSIGNED;
			continue;
		}
		if (c == 'l' || c == 'L') {
			if (modifiers & MOD_LONG)
				modifiers |= MOD_LONGLONG;
			modifiers |= MOD_LONG;
			continue;
		}
		break;
	}

	bits = BITS_IN_LONGLONG;
	extramod = 0;
	if (!(modifiers & MOD_LONGLONG)) {
		if (value & (~0ULL << BITS_IN_LONG)) {
			extramod = MOD_LONGLONG | MOD_LONG;
		} else {
			bits = BITS_IN_LONG;
			if (!(modifiers & MOD_LONG)) {
				if (value & (~0ULL << BITS_IN_INT)) {
					extramod = MOD_LONG;
				} else
					bits = BITS_IN_INT;
			}
		}
	}
	if (!(modifiers & MOD_UNSIGNED)) {
		if (value & (1ULL << (bits-1))) {
			extramod |= MOD_UNSIGNED;
		}
	}
	if (extramod) {
		/*
		 * Special case: "int" gets promoted directly to "long"
		 * for normal decimal numbers..
		 */
		modifiers |= extramod;
		if (base == 10 && modifiers == MOD_UNSIGNED) {
			modifiers = MOD_LONG;
			if (BITS_IN_LONG == BITS_IN_INT)
				modifiers = MOD_LONG | MOD_UNSIGNED;
		}
		warn(expr->token, "value is so big it is%s%s%s",
			(modifiers & MOD_UNSIGNED) ? " unsigned":"",
			(modifiers & MOD_LONG) ? " long":"",
			(modifiers & MOD_LONGLONG) ? " long":"");
	}

	expr->type = EXPR_VALUE;
	expr->ctype = ctype_integer(modifiers);
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

static int cast_value(struct expression *expr, struct symbol *newtype,
			struct expression *old, struct symbol *oldtype)
{
	int old_size = oldtype->bit_size;
	int new_size = newtype->bit_size;
	long long value, mask, ormask, andmask;
	int is_signed;

	// FIXME! We don't handle FP casts of constant values yet
	if (newtype->ctype.base_type == &fp_type)
		return 0;
	if (oldtype->ctype.base_type == &fp_type)
		return 0;

	// For pointers and integers, we can just move the value around
	expr->type = EXPR_VALUE;
	if (old_size == new_size) {
		expr->value = old->value;
		return 1;
	}

	// expand it to the full "long long" value
	is_signed = !(oldtype->ctype.modifiers & MOD_UNSIGNED);
	mask = 1ULL << (old_size-1);
	value = old->value;
	if (!(value & mask))
		is_signed = 0;
	andmask = mask | (mask-1);
	ormask = ~andmask;
	if (!is_signed)
		ormask = 0;
	value = (value & andmask) | ormask;

	// Truncate it to the new size
	mask = 1ULL << (new_size-1);
	mask = mask | (mask-1);
	expr->value = value & mask;
	return 1;
}

static struct expression * cast_to(struct expression *old, struct symbol *type)
{
	struct expression *expr = alloc_expression(old->token, EXPR_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	if (old->type == EXPR_VALUE)
		cast_value(expr, type, old, old->ctype);
	return expr;
}

static int is_ptr_type(struct symbol *type)
{
	return type->type == SYM_PTR || type->type == SYM_ARRAY || type->type == SYM_FN;
}

static int is_int_type(struct symbol *type)
{
	return type->ctype.base_type == &int_type;
}

static int bad_expr_type(struct expression *expr)
{
	warn(expr->token, "incompatible types for operation");
	return 0;
}

static struct symbol * compatible_integer_binop(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	/* Integer promotion? */
	if (ltype->type == SYM_ENUM)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM)
		rtype = &int_ctype;
	if (is_int_type(ltype) && is_int_type(rtype)) {
		struct symbol *ctype = bigger_int_type(ltype, rtype);

		/* Don't bother promoting same-size entities, it only adds clutter */
		if (ltype->bit_size != ctype->bit_size)
			expr->left = cast_to(left, ctype);
		if (rtype->bit_size != ctype->bit_size)
			expr->right = cast_to(right, ctype);
		return ctype;
	}
	return NULL;
}

static int evaluate_int_binop(struct expression *expr)
{
	struct symbol *ctype = compatible_integer_binop(expr);
	if (ctype) {
		expr->ctype = ctype;
		return 1;
	}
	return bad_expr_type(expr);
}

/* Arrays degenerate into pointers on pointer arithmetic */
static struct symbol *degenerate(struct expression *expr, struct symbol *ctype)
{
	if (ctype->type == SYM_ARRAY) {
		struct symbol *sym = alloc_symbol(expr->token, SYM_PTR);
		sym->ctype = ctype->ctype;
		ctype = sym;
	}
	return ctype;
}

static int evaluate_ptr_add(struct expression *expr, struct expression *ptr, struct expression *i)
{
	struct symbol *ctype;
	struct symbol *ptr_type = ptr->ctype;
	struct symbol *i_type = i->ctype;

	if (i_type->type == SYM_ENUM)
		i_type = &int_ctype;
	if (!is_int_type(i_type))
		return bad_expr_type(expr);

	ctype = ptr_type->ctype.base_type;
	examine_symbol_type(ctype);

	expr->ctype = degenerate(expr, ptr_type);
	if (ctype->bit_size > BITS_IN_CHAR) {
		struct expression *add = expr;
		struct expression *mul = alloc_expression(expr->token, EXPR_BINOP);
		struct expression *val = alloc_expression(expr->token, EXPR_VALUE);

		val->ctype = size_t_ctype;
		val->value = ctype->bit_size >> 3;

		mul->op = '*';
		mul->ctype = size_t_ctype;
		mul->left = i;
		mul->right = val;

		/* Leave 'add->op' as 'expr->op' - either '+' or '-' */
		add->ctype = ptr_type;
		add->left = ptr;
		add->right = mul;
	}
		
	return 1;
}

static int evaluate_plus(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (is_ptr_type(ltype))
		return evaluate_ptr_add(expr, left, right);

	if (is_ptr_type(rtype))
		return evaluate_ptr_add(expr, right, left);
		
	// FIXME! FP promotion
	return evaluate_int_binop(expr);
}

static struct symbol *same_ptr_types(struct symbol *a, struct symbol *b)
{
	a = a->ctype.base_type;
	b = b->ctype.base_type;

	if (a->bit_size != b->bit_size)
		return NULL;

	// FIXME! We should really check a bit more..
	return a;
}

static int evaluate_ptr_sub(struct expression *expr, struct expression *l, struct expression *r)
{
	struct symbol *ctype;
	struct symbol *ltype = l->ctype, *rtype = r->ctype;

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, l, r);


	ctype = same_ptr_types(ltype, rtype);
	if (!ctype) {
		warn(expr->token, "subtraction of different types can't work");
		return 0;
	}
	examine_symbol_type(ctype);

	expr->ctype = ssize_t_ctype;
	if (ctype->bit_size > BITS_IN_CHAR) {
		struct expression *sub = alloc_expression(expr->token, EXPR_BINOP);
		struct expression *div = expr;
		struct expression *val = alloc_expression(expr->token, EXPR_VALUE);

		val->ctype = size_t_ctype;
		val->value = ctype->bit_size >> 3;

		sub->op = '-';
		sub->ctype = ssize_t_ctype;
		sub->left = l;
		sub->right = r;

		div->op = '/';
		div->left = sub;
		div->right = val;
	}
		
	return 1;
}

static int evaluate_minus(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype;

	if (is_ptr_type(ltype))
		return evaluate_ptr_sub(expr, left, right);

	// FIXME! FP promotion
	return evaluate_int_binop(expr);
}

static int evaluate_logical(struct expression *expr)
{
	// FIXME! Short-circuit, FP and pointers!
	expr->ctype = &bool_ctype;
	return 1;
}

static int evaluate_arithmetic(struct expression *expr)
{
	// FIXME! Floating-point promotion!
	return evaluate_int_binop(expr);
}

static int evaluate_binop(struct expression *expr)
{
	switch (expr->op) {
	// addition can take ptr+int, fp and int
	case '+':
		return evaluate_plus(expr);

	// subtraction can take ptr-ptr, fp and int
	case '-':
		return evaluate_minus(expr);

	// Logical ops can take a lot of special stuff and have early-out
	case SPECIAL_LOGICAL_AND:
	case SPECIAL_LOGICAL_OR:
		return evaluate_logical(expr);

	// Arithmetic operations can take fp and int
	case '*': case '/': case '%':
		return evaluate_arithmetic(expr);

	// The rest are integer operations (bitops)
	// SPECIAL_LEFTSHIFT, SPECIAL_RIGHTSHIFT
	// '&', '^', '|'
	default:
		return evaluate_int_binop(expr);
	}
}

static int evaluate_comma(struct expression *expr)
{
	expr->ctype = expr->right->ctype;
	return 1;
}

static int evaluate_compare(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	/* Pointer types? */
	if (is_ptr_type(ltype) || is_ptr_type(rtype)) {
		expr->ctype = &bool_ctype;
		// FIXME! Check the types for compatibility
		return 1;
	}

	if (compatible_integer_binop(expr)) {
		expr->ctype = &bool_ctype;
		return 1;
	}

	return bad_expr_type(expr);
}

static int evaluate_assignment(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	// FIXME! We need to cast and check the rigth side!
	if (ltype->bit_size != rtype->bit_size)
		expr->right = cast_to(right, ltype);
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
	struct symbol *ctype = expr->unop->ctype;
	expr->ctype = ctype;
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
		/* Arrays will degenerate into pointers for '->' */
		if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
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

static int evaluate_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;
	struct symbol *ctype = expr->cast_type;

	if (!evaluate_expression(target))
		return 0;
	examine_symbol_type(ctype);
	expr->ctype = ctype;

	/* Simplify normal integer casts.. */
	if (target->type == EXPR_VALUE)
		cast_value(expr, ctype, target, target->ctype);
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
	if (ctype->type == SYM_PTR || ctype->type == SYM_ARRAY)
		ctype = ctype->ctype.base_type;
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
		return evaluate_cast(expr);
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
