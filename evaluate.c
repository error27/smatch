/*
 * sparse/evaluate.c
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
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

static int current_context, current_contextmask;

static struct symbol *evaluate_symbol_expression(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		warn(expr->pos, "undefined identifier '%s'", show_ident(expr->symbol_name));
		return NULL;
	}

	examine_symbol_type(sym);
	if ((sym->ctype.context ^ current_context) & (sym->ctype.contextmask & current_contextmask))
		warn(expr->pos, "Using symbol '%s' in wrong context", show_ident(expr->symbol_name));

	base_type = sym->ctype.base_type;
	if (!base_type) {
		warn(sym->pos, "identifier '%s' has no type", show_ident(expr->symbol_name));
		return NULL;
	}

	/* The type of a symbol is the symbol itself! */
	expr->ctype = sym;

	/* enum's can be turned into plain values */
	if (base_type->type == SYM_ENUM) {
		expr->type = EXPR_VALUE;
		expr->value = sym->value;
	}
	return sym;
}

static struct symbol *evaluate_string(struct expression *expr)
{
	struct symbol *sym = alloc_symbol(expr->pos, SYM_ARRAY);
	int length = expr->string->length;

	sym->array_size = length;
	sym->bit_size = BITS_IN_CHAR * length;
	sym->ctype.alignment = 1;
	sym->ctype.modifiers = MOD_CONST;
	sym->ctype.base_type = &char_ctype;
	expr->ctype = sym;
	return sym;
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

static struct symbol * cast_value(struct expression *expr, struct symbol *newtype,
			struct expression *old, struct symbol *oldtype)
{
	int old_size = oldtype->bit_size;
	int new_size = newtype->bit_size;
	long long value, mask, ormask, andmask;
	int is_signed;

	// FIXME! We don't handle FP casts of constant values yet
	if (newtype->ctype.base_type == &fp_type)
		return NULL;
	if (oldtype->ctype.base_type == &fp_type)
		return NULL;

	// For pointers and integers, we can just move the value around
	expr->type = EXPR_VALUE;
	if (old_size == new_size) {
		expr->value = old->value;
		return newtype;
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
	return newtype;
}

static struct expression * cast_to(struct expression *old, struct symbol *type)
{
	struct expression *expr = alloc_expression(old->pos, EXPR_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	if (old->type == EXPR_VALUE)
		cast_value(expr, type, old, old->ctype);
	return expr;
}

static int is_ptr_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->type == SYM_PTR || type->type == SYM_ARRAY || type->type == SYM_FN;
}

static int is_int_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->ctype.base_type == &int_type;
}

static struct symbol *bad_expr_type(struct expression *expr)
{
	warn(expr->pos, "incompatible types for operation");
	return NULL;
}

static struct symbol * compatible_integer_binop(struct expression *expr, struct expression **lp, struct expression **rp)
{
	struct expression *left = *lp, *right = *rp;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
	/* Integer promotion? */
	if (ltype->type == SYM_ENUM)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM)
		rtype = &int_ctype;
	if (is_int_type(ltype) && is_int_type(rtype)) {
		struct symbol *ctype = bigger_int_type(ltype, rtype);

		/* Don't bother promoting same-size entities, it only adds clutter */
		if (ltype->bit_size != ctype->bit_size)
			*lp = cast_to(left, ctype);
		if (rtype->bit_size != ctype->bit_size)
			*rp = cast_to(right, ctype);
		return ctype;
	}
	return NULL;
}

static struct symbol *evaluate_int_binop(struct expression *expr)
{
	struct symbol *ctype = compatible_integer_binop(expr, &expr->left, &expr->right);
	if (ctype) {
		expr->ctype = ctype;
		return ctype;
	}
	return bad_expr_type(expr);
}

/* Arrays degenerate into pointers on pointer arithmetic */
static struct symbol *degenerate(struct expression *expr, struct symbol *ctype)
{
	if (ctype->type == SYM_ARRAY) {
		struct symbol *sym = alloc_symbol(expr->pos, SYM_PTR);
		sym->ctype = ctype->ctype;
		sym->bit_size = BITS_IN_POINTER;
		sym->ctype.alignment = POINTER_ALIGNMENT;
		ctype = sym;
	}
	return ctype;
}

static struct symbol *evaluate_ptr_add(struct expression *expr, struct expression *ptr, struct expression *i)
{
	struct symbol *ctype;
	struct symbol *ptr_type = ptr->ctype;
	struct symbol *i_type = i->ctype;

	if (i_type->type == SYM_NODE)
		i_type = i_type->ctype.base_type;
	if (ptr_type->type == SYM_NODE)
		ptr_type = ptr_type->ctype.base_type;

	if (i_type->type == SYM_ENUM)
		i_type = &int_ctype;
	if (!is_int_type(i_type))
		return bad_expr_type(expr);

	ctype = ptr_type->ctype.base_type;
	examine_symbol_type(ctype);

	expr->ctype = degenerate(expr, ptr_type);
	if (ctype->bit_size > BITS_IN_CHAR) {
		struct expression *add = expr;
		struct expression *mul = alloc_expression(expr->pos, EXPR_BINOP);
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);

		val->ctype = size_t_ctype;
		val->value = ctype->bit_size >> 3;

		mul->op = '*';
		mul->ctype = size_t_ctype;
		mul->left = i;
		mul->right = val;

		/* Leave 'add->op' as 'expr->op' - either '+' or '-' */
		add->left = ptr;
		add->right = mul;
	}
		
	return expr->ctype;
}

static struct symbol *evaluate_add(struct expression *expr)
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

static int same_type(struct symbol *target, struct symbol *source)
{
	int dropped_modifiers = 0;
	for (;;) {
		unsigned long mod1, mod2;
		unsigned long as1, as2;

		if (target == source)
			break;
		if (!target || !source)
			return 0;
		/*
		 * Peel of per-node information.
		 * FIXME! Check alignment, address space, and context too here!
		 */
		mod1 = target->ctype.modifiers;
		as1 = target->ctype.as;
		mod2 = source->ctype.modifiers;
		as2 = source->ctype.as;
		if (target->type == SYM_NODE) {
			target = target->ctype.base_type;
			mod1 |= target->ctype.modifiers;
			as1 |= target->ctype.as;
		}
		if (source->type == SYM_NODE) {
			source = source->ctype.base_type;
			mod2 |= source->ctype.modifiers;
			as2 |= source->ctype.as;
		}

		/* Ignore differences in storage types */
		if ((mod1 ^ mod2) & ~MOD_STORAGE)
			return 0;

		/* Must be same address space to be comparable */
		if (as1 != as2)
			return 0;
	
		if (target->type != source->type) {
			int type1 = target->type;
			int type2 = source->type;

			/* Ignore ARRAY/PTR differences, as long as they point to the same type */
			type1 = type1 == SYM_ARRAY ? SYM_PTR : type1;
			type2 = type2 == SYM_ARRAY ? SYM_PTR : type2;
			if (type1 != type2)
				return 0;
		}

		target = target->ctype.base_type;
		source = source->ctype.base_type;
	}
	if (dropped_modifiers)
		warn(target->pos, "assignment drops modifiers");
	return 1;
}

static struct symbol *common_ptr_type(struct expression *l, struct expression *r)
{
	/* NULL expression? Just return the type of the "other side" */
	if (r->type == EXPR_VALUE && !r->value)
		return l->ctype;
	if (l->type == EXPR_VALUE && !l->value)
		return r->ctype;
	return NULL;
}

static struct symbol *evaluate_ptr_sub(struct expression *expr, struct expression *l, struct expression *r)
{
	struct symbol *ctype;
	struct symbol *ltype = l->ctype, *rtype = r->ctype;

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, l, r);

	ctype = ltype;
	if (!same_type(ltype, rtype)) {
		ctype = common_ptr_type(l, r);
		if (!ctype) {
			warn(expr->pos, "subtraction of different types can't work");
			return NULL;
		}
	}
	examine_symbol_type(ctype);

	/* Figure out the base type we point to */
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
		warn(expr->pos, "subtraction of functions? Share your drugs");
		return NULL;
	}
	ctype = ctype->ctype.base_type;

	expr->ctype = ssize_t_ctype;
	if (ctype->bit_size > BITS_IN_CHAR) {
		struct expression *sub = alloc_expression(expr->pos, EXPR_BINOP);
		struct expression *div = expr;
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);

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
		
	return ssize_t_ctype;
}

static struct symbol *evaluate_sub(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype;

	if (is_ptr_type(ltype))
		return evaluate_ptr_sub(expr, left, right);

	// FIXME! FP promotion
	return evaluate_int_binop(expr);
}

static struct symbol *evaluate_logical(struct expression *expr)
{
	// FIXME! Short-circuit, FP and pointers!
	expr->ctype = &bool_ctype;
	return &bool_ctype;
}

static struct symbol *evaluate_arithmetic(struct expression *expr)
{
	// FIXME! Floating-point promotion!
	return evaluate_int_binop(expr);
}

static struct symbol *evaluate_binop(struct expression *expr)
{
	switch (expr->op) {
	// addition can take ptr+int, fp and int
	case '+':
		return evaluate_add(expr);

	// subtraction can take ptr-ptr, fp and int
	case '-':
		return evaluate_sub(expr);

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

static struct symbol *evaluate_comma(struct expression *expr)
{
	expr->ctype = expr->right->ctype;
	return expr->ctype;
}

static struct symbol *evaluate_compare(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	/* Pointer types? */
	if (is_ptr_type(ltype) || is_ptr_type(rtype)) {
		expr->ctype = &bool_ctype;
		// FIXME! Check the types for compatibility
		return &bool_ctype;
	}

	if (compatible_integer_binop(expr, &expr->left, &expr->right)) {
		expr->ctype = &bool_ctype;
		return &bool_ctype;
	}

	return bad_expr_type(expr);
}

static int compatible_integer_types(struct symbol *ltype, struct symbol *rtype)
{
	/* Integer promotion? */
	if (ltype->type == SYM_ENUM)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM)
		rtype = &int_ctype;
	return (is_int_type(ltype) && is_int_type(rtype));
}

static int is_void_ptr(struct expression *expr)
{
	return (expr->type == EXPR_VALUE &&
		expr->value == 0);
}

/*
 * FIXME!! This shoul ddo casts, array degeneration etc..
 */
static struct symbol *compatible_ptr_type(struct expression *left, struct expression *right)
{
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_PTR) {
		if (is_void_ptr(right))
			return ltype;
	}

	if (rtype->type == SYM_PTR) {
		if (is_void_ptr(left))
			return rtype;
	}
	return NULL;
}

static struct symbol * evaluate_conditional(struct expression *expr)
{
	struct symbol *ctype;
	struct symbol *ltype = expr->cond_true->ctype;
	struct symbol *rtype = expr->cond_false->ctype;

	if (same_type(ltype, rtype)) {
		expr->ctype = ltype;
		return ltype;
	}

	ctype = compatible_integer_binop(expr, &expr->cond_true, &expr->cond_false);
	if (ctype) {
		expr->ctype = ctype;
		return ctype;
	}

	ctype = compatible_ptr_type(expr->cond_true, expr->cond_false);
	if (ctype) {
		expr->ctype = ctype;
		return ctype;
	}

	warn(expr->pos, "incompatible types in conditional expression");
	return NULL;
}
		
static int compatible_assignment_types(struct expression *expr, struct symbol *target,
	struct expression **rp, struct symbol *source)
{
	if (same_type(target, source))
		return 1;

	if (compatible_integer_types(target, source)) {
		if (target->bit_size != source->bit_size)
			*rp = cast_to(*rp, target);
		return 1;
	}

	/* Pointer destination? */
	if (target->type == SYM_NODE)
		target = target->ctype.base_type;
	if (source->type == SYM_NODE)
		source = source->ctype.base_type;
	if (target->type == SYM_PTR) {
		struct expression *right = *rp;
		struct symbol *source_base = source->ctype.base_type;
		struct symbol *target_base = target->ctype.base_type;

		if (source->type == SYM_NODE) {
			source = source_base;
			source_base = source->ctype.base_type;
		}
		if (target->type == SYM_NODE) {
			target = target_base;
			target_base = target->ctype.base_type;
		}
		if (source->type == SYM_ARRAY && same_type(target_base, source_base))
			return 1;
		if (source->type == SYM_FN && same_type(target_base, source))
			return 1;

		// NULL pointer?
		if (right->type == EXPR_VALUE && !right->value)
			return 1;

		// void pointer ?
		if (target->type == SYM_PTR) {
			struct symbol *source_base = source->ctype.base_type;
			struct symbol *target_base = target->ctype.base_type;
			if (source_base == &void_ctype || target_base == &void_ctype)
				return 1;
			warn(expr->pos, "assignment from incompatible pointer types");
			return 1;
		}

		// FIXME!! Cast it!
		warn(expr->pos, "assignment from different types");
		return 0;
	}

	// FIXME!! Cast it!
	warn(expr->pos, "assignment from bad type");
	return 0;
}

static struct symbol *evaluate_binop_assignment(struct expression *expr, struct expression *left, struct expression *right)
{
	int op = expr->op;
	struct expression *subexpr = alloc_expression(expr->pos, EXPR_BINOP);
	static const int op_trans[] = {
		[SPECIAL_ADD_ASSIGN - SPECIAL_BASE] = '+',
		[SPECIAL_SUB_ASSIGN - SPECIAL_BASE] = '-',
		[SPECIAL_MUL_ASSIGN - SPECIAL_BASE] = '*',
		[SPECIAL_DIV_ASSIGN - SPECIAL_BASE] = '/',
		[SPECIAL_MOD_ASSIGN - SPECIAL_BASE] = '%',
		[SPECIAL_SHL_ASSIGN - SPECIAL_BASE] = SPECIAL_LEFTSHIFT,
		[SPECIAL_SHR_ASSIGN - SPECIAL_BASE] = SPECIAL_RIGHTSHIFT,
		[SPECIAL_AND_ASSIGN - SPECIAL_BASE] = '&',
		[SPECIAL_OR_ASSIGN - SPECIAL_BASE] = '&',
		[SPECIAL_XOR_ASSIGN - SPECIAL_BASE] = '^'
	};

	subexpr->left = left;
	subexpr->right = right;
	subexpr->op = op_trans[op - SPECIAL_BASE];
	expr->op = '=';
	expr->right = subexpr;
	return evaluate_binop(subexpr);
}

static struct symbol *evaluate_assignment(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype, *rtype;

	ltype = left->ctype;
	rtype = right->ctype;
	if (expr->op != '=') {
		rtype = evaluate_binop_assignment(expr, left, right);
		if (!rtype)
			return 0;
		right = expr->right;
	}

	if (!ltype) {
		warn(expr->pos, "what? no ltype");
		return 0;
	}
	if (!rtype) {
		warn(expr->pos, "what? no rtype");
		return 0;
	}

	if (!compatible_assignment_types(expr, ltype, &expr->right, rtype))
		return 0;

	expr->ctype = expr->left->ctype;
	return expr->ctype;
}

static struct symbol *evaluate_addressof(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;
	struct symbol *symbol = alloc_symbol(expr->pos, SYM_PTR);

	symbol->ctype.base_type = ctype;
	symbol->ctype.alignment = POINTER_ALIGNMENT;
	symbol->bit_size = BITS_IN_POINTER;
	expr->ctype = symbol;
	if (expr->unop->type == EXPR_SYMBOL) {
		struct symbol *var = expr->unop->symbol;
		if (var->ctype.modifiers & MOD_REGISTER) {
			warn(expr->pos, "register variable and address-of do not mix");
			var->ctype.modifiers &= ~MOD_REGISTER;
		}
		var->ctype.modifiers |= MOD_ADDRESSABLE;
	}
	return symbol;
}



static struct symbol *evaluate_preop(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;
	unsigned long mod;

	switch (expr->op) {
	case '(':
		*expr = *expr->unop;
		return ctype;

	case '*':
		mod = ctype->ctype.modifiers;
		if (ctype->type == SYM_NODE) {
			ctype = ctype->ctype.base_type;
			mod |= ctype->ctype.modifiers;
		}
		if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
			warn(expr->pos, "cannot derefence this type");
			return 0;
		}
		if (mod & MOD_NODEREF)
			warn(expr->pos, "bad dereference");
		ctype = ctype->ctype.base_type;
		if (!ctype) {
			warn(expr->pos, "undefined type");
			return 0;
		}
		examine_symbol_type(ctype);
		expr->ctype = ctype;
		return ctype;

	case '&':
		return evaluate_addressof(expr);

	case '!':
		expr->ctype = &bool_ctype;
		return &bool_ctype;

	default:
		expr->ctype = ctype;
		return ctype;
	}
}

/*
 * Unary post-ops: x++ and x--
 */
static struct symbol *evaluate_postop(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;
	expr->ctype = ctype;
	return ctype;
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
				if (sym->ident != ident)
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
static struct symbol *evaluate_dereference(struct expression *expr)
{
	int offset;
	struct symbol *ctype, *member;
	struct expression *deref = expr->deref, *add;
	struct ident *ident = expr->member;
	unsigned int mod;

	if (!evaluate_expression(deref))
		return NULL;
	if (!ident) {
		warn(expr->pos, "bad member name");
		return NULL;
	}

	ctype = deref->ctype;
	mod = ctype->ctype.modifiers;
	if (ctype->type == SYM_NODE) {
		ctype = ctype->ctype.base_type;
		mod |= ctype->ctype.modifiers;
	}
	if (expr->op == SPECIAL_DEREFERENCE) {
		/* Arrays will degenerate into pointers for '->' */
		if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
			warn(expr->pos, "expected a pointer to a struct/union");
			return NULL;
		}
		ctype = ctype->ctype.base_type;
		mod |= ctype->ctype.modifiers;
		if (ctype->type == SYM_NODE) {
			ctype = ctype->ctype.base_type;
			mod |= ctype->ctype.modifiers;
		}
	}
	if (mod & MOD_NODEREF)
		warn(expr->pos, "bad dereference");
	if (!ctype || (ctype->type != SYM_STRUCT && ctype->type != SYM_UNION)) {
		warn(expr->pos, "expected structure or union");
		return NULL;
	}
	offset = 0;
	member = find_identifier(ident, ctype->symbol_list, &offset);
	if (!member) {
		warn(expr->pos, "no such struct/union member");
		return NULL;
	}

	add = deref;
	if (offset != 0) {
		add = alloc_expression(expr->pos, EXPR_BINOP);
		add->op = '+';
		add->ctype = &ptr_ctype;
		add->left = deref;
		add->right = alloc_expression(expr->pos, EXPR_VALUE);
		add->right->ctype = &int_ctype;
		add->right->value = offset;
	}

	ctype = member->ctype.base_type;
	if (ctype->type == SYM_BITFIELD) {
		ctype = ctype->ctype.base_type;
		expr->type = EXPR_BITFIELD;
		expr->bitpos = member->bit_offset;
		expr->nrbits = member->fieldwidth;
		expr->address = add;
	} else {
		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->unop = add;
	}

	expr->ctype = ctype;
	return ctype;
}

static struct symbol *evaluate_sizeof(struct expression *expr)
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
		warn(expr->pos, "cannot size expression");
		return 0;
	}
	expr->type = EXPR_VALUE;
	expr->value = size >> 3;
	expr->ctype = size_t_ctype;
	return size_t_ctype;
}

static struct symbol *evaluate_lvalue_expression(struct expression *expr)
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

/*
 * FIXME! This is bogus: we need to take array index
 * entries into account when calculating the size of
 * the array.
 */
static int count_array_initializer(struct expression *expr)
{
	struct expression_list *list;

	if (expr->type != EXPR_INITIALIZER)
		return 1;
	list = expr->expr_list;
	return expression_list_size(list);
}

/*
 * Initializers are kind of like assignments. Except
 * they can be a hell of a lot more complex.
 */
static int evaluate_initializer(struct symbol *ctype, struct expression **ep)
{
	struct expression *expr = *ep;
	/*
	 * Simple non-structure/array initializers are the simple 
	 * case, and look (and parse) largely like assignments.
	 */
	if (expr->type != EXPR_INITIALIZER) {
		struct symbol *rtype = evaluate_expression(expr);
		if (rtype)
			compatible_assignment_types(expr, ctype, ep, rtype);
		return 0;
	}

	/*
	 * FIXME!! Check type compatibility, and look up any named
	 * initializers and index expressions!
	 */
	expr->ctype = ctype;
	return count_array_initializer(expr);
}

static struct symbol *evaluate_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;
	struct symbol *ctype = examine_symbol_type(expr->cast_type);

	expr->ctype = ctype;
	expr->cast_type = ctype;

	/*
	 * Special case: a cast can be followed by an
	 * initializer, in which case we need to pass
	 * the type value down to that initializer rather
	 * than trying to evaluate it as an expression
	 */
	if (target->type == EXPR_INITIALIZER) {
		evaluate_initializer(ctype, &expr->cast_expression);
		return ctype;
	}

	evaluate_expression(target);

	/* Simplify normal integer casts.. */
	if (target->type == EXPR_VALUE)
		cast_value(expr, ctype, target, target->ctype);
	return ctype;
}

static struct symbol *evaluate_call(struct expression *expr)
{
	int args, fnargs;
	struct symbol *ctype;
	struct expression *fn = expr->fn;
	struct expression_list *arglist = expr->args;

	if (!evaluate_expression(fn))
		return NULL;
	if (!evaluate_expression_list(arglist))
		return NULL;
	ctype = fn->ctype;
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type == SYM_PTR || ctype->type == SYM_ARRAY)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_FN) {
		warn(expr->pos, "not a function");
		return NULL;
	}
	args = expression_list_size(expr->args);
	fnargs = symbol_list_size(ctype->arguments);
	if (args < fnargs)
		warn(expr->pos, "not enough arguments for function");
	if (args > fnargs && !ctype->variadic)
		warn(expr->pos, "too many arguments for function");
	expr->ctype = ctype->ctype.base_type;
	return expr->ctype;
}

struct symbol *evaluate_expression(struct expression *expr)
{
	if (!expr)
		return NULL;
	if (expr->ctype)
		return expr->ctype;

	switch (expr->type) {
	case EXPR_VALUE:
		warn(expr->pos, "value expression without a type");
		return NULL;
	case EXPR_STRING:
		return evaluate_string(expr);
	case EXPR_SYMBOL:
		return evaluate_symbol_expression(expr);
	case EXPR_BINOP:
		if (!evaluate_expression(expr->left))
			return NULL;
		if (!evaluate_expression(expr->right))
			return NULL;
		return evaluate_binop(expr);
	case EXPR_COMMA:
		if (!evaluate_expression(expr->left))
			return NULL;
		if (!evaluate_expression(expr->right))
			return NULL;
		return evaluate_comma(expr);
	case EXPR_COMPARE:
		if (!evaluate_expression(expr->left))
			return NULL;
		if (!evaluate_expression(expr->right))
			return NULL;
		return evaluate_compare(expr);
	case EXPR_ASSIGNMENT:
		if (!evaluate_lvalue_expression(expr->left))
			return NULL;
		if (!evaluate_expression(expr->right))
			return NULL;
		return evaluate_assignment(expr);
	case EXPR_PREOP:
		if (!evaluate_expression(expr->unop))
			return NULL;
		return evaluate_preop(expr);
	case EXPR_POSTOP:
		if (!evaluate_expression(expr->unop))
			return NULL;
		return evaluate_postop(expr);
	case EXPR_CAST:
		return evaluate_cast(expr);
	case EXPR_SIZEOF:
		return evaluate_sizeof(expr);
	case EXPR_DEREF:
		return evaluate_dereference(expr);
	case EXPR_CALL:
		return evaluate_call(expr);
	case EXPR_BITFIELD:
		warn(expr->pos, "bitfield generated by parser");
		return NULL;
	case EXPR_CONDITIONAL:
		if (!evaluate_expression(expr->conditional) ||
		    !evaluate_expression(expr->cond_true) ||
		    !evaluate_expression(expr->cond_false))
			return NULL;
		return evaluate_conditional(expr);
	case EXPR_STATEMENT:
		expr->ctype = evaluate_statement(expr->statement);
		return expr->ctype;

	/* These can not exist as stand-alone expressions */
	case EXPR_INITIALIZER:
	case EXPR_IDENTIFIER:
	case EXPR_INDEX:
		warn(expr->pos, "internal front-end error: initializer in expression");
		return NULL;
	}
	return NULL;
}

static void evaluate_one_statement(struct statement *stmt, void *_last, int flags)
{
	struct symbol **last = _last;
	struct symbol *type = evaluate_statement(stmt);

	if (flags & ITERATE_LAST)
		*last = type;
}

static void evaluate_one_symbol(struct symbol *sym, void *unused, int flags)
{
	evaluate_symbol(sym);
}

struct symbol *evaluate_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return NULL;
	sym->ctype.base_type = base_type;

	/* Evaluate the initializers */
	if (sym->initializer) {
		int count = evaluate_initializer(base_type, &sym->initializer);
		if (base_type->type == SYM_ARRAY && base_type->array_size < 0) {
			int bit_size = count * base_type->ctype.base_type->bit_size;
			base_type->array_size = count;
			base_type->bit_size = bit_size;
			sym->array_size = count;
			sym->bit_size = bit_size;
		}
	}

	/* And finally, evaluate the body of the symbol too */
	if (base_type->type == SYM_FN) {
		symbol_iterate(base_type->arguments, evaluate_one_symbol, NULL);
		if (base_type->stmt) {
			current_contextmask = sym->ctype.contextmask;
			current_context = sym->ctype.context;
			evaluate_statement(base_type->stmt);
		}
	}

	return base_type;
}

struct symbol *evaluate_statement(struct statement *stmt)
{
	if (!stmt)
		return NULL;

	switch (stmt->type) {
	case STMT_RETURN:
	case STMT_EXPRESSION:
		return evaluate_expression(stmt->expression);

	case STMT_COMPOUND: {
		struct symbol *type = NULL;
		symbol_iterate(stmt->syms, evaluate_one_symbol, NULL);
		statement_iterate(stmt->stmts, evaluate_one_statement, &type);
		return type;
	}
	case STMT_IF:
		evaluate_expression(stmt->if_conditional);
		evaluate_statement(stmt->if_true);
		evaluate_statement(stmt->if_false);
		return NULL;
	case STMT_ITERATOR:
		evaluate_expression(stmt->iterator_pre_condition);
		evaluate_expression(stmt->iterator_post_condition);
		evaluate_statement(stmt->iterator_pre_statement);
		evaluate_statement(stmt->iterator_statement);
		evaluate_statement(stmt->iterator_post_statement);
		return NULL;
	case STMT_SWITCH:
		evaluate_expression(stmt->switch_expression);
		evaluate_statement(stmt->switch_statement);
		return NULL;
	case STMT_CASE:
		evaluate_expression(stmt->case_expression);
		evaluate_expression(stmt->case_to);
		evaluate_statement(stmt->case_statement);
		return NULL;
	case STMT_LABEL:
		evaluate_statement(stmt->label_statement);
		return NULL;
	case STMT_GOTO:
		evaluate_expression(stmt->goto_expression);
		return NULL;
	case STMT_NONE:
	case STMT_BREAK:
	case STMT_CONTINUE:
		break;
	case STMT_ASM:
		/* FIXME! Do the asm parameter evaluation! */
		break;
	}
	return NULL;
}

long long get_expression_value(struct expression *expr)
{
	long long left, middle, right;

	switch (expr->type) {
	case EXPR_SIZEOF:
		if (expr->cast_type) {
			examine_symbol_type(expr->cast_type);
			if (expr->cast_type->bit_size & 7) {
				warn(expr->pos, "type has no size");
				return 0;
			}
			return expr->cast_type->bit_size >> 3;
		}
		warn(expr->pos, "expression sizes not yet supported");
		return 0;
	case EXPR_VALUE:
		return expr->value;
	case EXPR_SYMBOL: {
		struct symbol *sym = expr->symbol;
		if (!sym || !sym->ctype.base_type || sym->ctype.base_type->type != SYM_ENUM) {
			warn(expr->pos, "undefined identifier '%s' in constant expression", show_ident(expr->symbol_name));
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
	error(expr->pos, "bad constant expression");
	return 0;
}
