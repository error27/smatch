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
#include <stdio.h>
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

static struct symbol *current_fn;
static int current_context, current_contextmask;

static struct symbol *evaluate_symbol_expression(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		if (preprocessing) {
			warn(expr->pos, "undefined preprocessor identifier '%s'", show_ident(expr->symbol_name));
			expr->type = EXPR_VALUE;
			expr->value = 0;
			expr->ctype = &int_ctype;
			return &int_ctype;
		}
		warn(expr->pos, "undefined identifier '%s'", show_ident(expr->symbol_name));
		return NULL;
	}

	examine_symbol_type(sym);
	if ((sym->ctype.context ^ current_context) & (sym->ctype.contextmask & current_contextmask))
		warn(expr->pos, "Using symbol '%s' in wrong context", show_ident(expr->symbol_name));

	base_type = sym->ctype.base_type;
	if (!base_type) {
		warn(expr->pos, "identifier '%s' has no type", show_ident(expr->symbol_name));
		return NULL;
	}

	/* The type of a symbol is the symbol itself! */
	expr->ctype = sym;

	/* enum's can be turned into plain values */
	if (base_type->type != SYM_ENUM) {
		struct expression *addr = alloc_expression(expr->pos, EXPR_SYMBOL);
		addr->symbol = sym;
		addr->symbol_name = expr->symbol_name;
		addr->ctype = &ptr_ctype;
		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->unop = addr;
		return sym;
	}
	expr->type = EXPR_VALUE;
	expr->value = sym->value;
	return sym;
}

static struct symbol *evaluate_string(struct expression *expr)
{
	struct symbol *sym = alloc_symbol(expr->pos, SYM_NODE);
	struct symbol *array = alloc_symbol(expr->pos, SYM_ARRAY);
	struct expression *addr = alloc_expression(expr->pos, EXPR_SYMBOL);
	int length = expr->string->length;

	sym->array_size = length;
	sym->bit_size = BITS_IN_CHAR * length;
	sym->ctype.alignment = 1;
	sym->ctype.modifiers = MOD_STATIC;
	sym->ctype.base_type = array;

	array->array_size = length;
	array->bit_size = BITS_IN_CHAR * length;
	array->ctype.alignment = 1;
	array->ctype.modifiers = MOD_STATIC;
	array->ctype.base_type = &char_ctype;
	
	addr->symbol = sym;
	addr->ctype = &ptr_ctype;

	expr->type = EXPR_PREOP;
	expr->op = '*';
	expr->unop = addr;  
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
	if (ltype->type == SYM_ENUM || ltype->type == SYM_BITFIELD)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM || rtype->type == SYM_BITFIELD)
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

static int check_shift_count(struct expression *expr, struct symbol *ctype, unsigned int count)
{
	if (count >= ctype->bit_size) {
		warn(expr->pos, "shift too big for type");
		count &= ctype->bit_size-1;
	}
	return count;
}

/*
 * CAREFUL! We need to get the size and sign of the
 * result right!
 */
static void simplify_int_binop(struct expression *expr, struct symbol *ctype)
{
	struct expression *left = expr->left, *right = expr->right;
	unsigned long long v, l, r, mask;
	signed long long s, sl, sr;
	int is_signed, shift;

	if (left->type != EXPR_VALUE || right->type != EXPR_VALUE)
		return;
	l = left->value; r = right->value;
	is_signed = !(ctype->ctype.modifiers & MOD_UNSIGNED);
	mask = 1ULL << (ctype->bit_size-1);
	sl = l; sr = r;
	if (is_signed && (sl & mask))
		sl |= ~(mask-1);
	if (is_signed && (sr & mask))
		sr |= ~(mask-1);
	
	switch (expr->op) {
	case '+':		v = l + r; s = v; break;
	case '-':		v = l - r; s = v; break;
	case '&':		v = l & r; s = v; break;
	case '|':		v = l | r; s = v; break;
	case '^':		v = l ^ r; s = v; break;
	case '*':		v = l * r; s = sl * sr; break;
	case '/':		if (!r) return; v = l / r; s = sl / sr; break;
	case '%':		if (!r) return; v = l % r; s = sl % sr; break;
	case SPECIAL_LEFTSHIFT: shift = check_shift_count(expr, ctype, r); v = l << shift; s = v; break; 
	case SPECIAL_RIGHTSHIFT:shift = check_shift_count(expr, ctype, r); v = l >> shift; s = sl >> shift; break;
	case '<':		v = l < r; s = sl < sr; break;
	case '>':		v = l > r; s = sl > sr; break;
	case SPECIAL_LTE:	v = l <= r; s = sl <= sr; break;
	case SPECIAL_GTE:	v = l >= r; s = sl >= sr; break;
	case SPECIAL_EQUAL:	v = l == r; s = v; break;
	case SPECIAL_NOTEQUAL:	v = l != r; s = v; break;
	default: return;
	}
	if (is_signed)
		v = s;
	mask = mask | (mask-1);
	expr->value = v & mask;
	expr->type = EXPR_VALUE;
}

static struct symbol *evaluate_int_binop(struct expression *expr)
{
	struct symbol *ctype = compatible_integer_binop(expr, &expr->left, &expr->right);
	if (ctype) {
		expr->ctype = ctype;
		simplify_int_binop(expr, ctype);
		return ctype;
	}
	return bad_expr_type(expr);
}

static inline int lvalue_expression(struct expression *expr)
{
	return (expr->type == EXPR_PREOP && expr->op == '*') || expr->type == EXPR_BITFIELD;
}

/* Arrays degenerate into pointers on pointer arithmetic */
static struct symbol *degenerate(struct expression *expr, struct symbol *ctype, struct expression **ptr_p)
{
	struct symbol *base = ctype;

	if (ctype->type == SYM_NODE)
		base = ctype->ctype.base_type;
	if (base->type == SYM_ARRAY) {
		struct symbol *sym = alloc_symbol(expr->pos, SYM_PTR);
		struct expression *ptr;

		merge_type(sym, ctype);
		merge_type(sym, base);
		sym->bit_size = BITS_IN_POINTER;
		ctype = sym;

		ptr = *ptr_p;
		*ptr_p = ptr->unop;

		/*
		 * This all really assumes that we got the degenerate
		 * array as an lvalue (ie a dereference). If that
		 * is not the case, then holler - because we've screwed
		 * up.
		 */
		if (!lvalue_expression(ptr))
			warn(ptr->pos, "internal error: strange degenerate array case");
		ptr->ctype = ctype;
	}
	return ctype;
}

static struct symbol *evaluate_ptr_add(struct expression *expr, struct expression *ptr, struct expression *i)
{
	struct symbol *ctype;
	struct symbol *ptr_type = ptr->ctype;
	struct symbol *i_type = i->ctype;
	int bit_size;

	if (i_type->type == SYM_NODE)
		i_type = i_type->ctype.base_type;
	if (ptr_type->type == SYM_NODE)
		ptr_type = ptr_type->ctype.base_type;

	if (i_type->type == SYM_ENUM)
		i_type = &int_ctype;
	if (!is_int_type(i_type))
		return bad_expr_type(expr);

	ctype = ptr->ctype;
	examine_symbol_type(ctype);

	ctype = degenerate(expr, ctype, &ptr);
	bit_size = ctype->bit_size;

	/* Special case: adding zero commonly happens as a result of 'array[0]' */
	if (i->type == EXPR_VALUE && !i->value) {
		*expr = *ptr;
	} else if (bit_size > BITS_IN_CHAR) {
		struct expression *add = expr;
		struct expression *mul = alloc_expression(expr->pos, EXPR_BINOP);
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);

		val->ctype = size_t_ctype;
		val->value = bit_size >> 3;

		mul->op = '*';
		mul->ctype = size_t_ctype;
		mul->left = i;
		mul->right = val;
		simplify_int_binop(mul, size_t_ctype);

		/* Leave 'add->op' as 'expr->op' - either '+' or '-' */
		add->left = ptr;
		add->right = mul;
		simplify_int_binop(add, ctype);
	}

	expr->ctype = ctype;	
	return ctype;
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

#define MOD_SIZE (MOD_CHAR | MOD_SHORT | MOD_LONG | MOD_LONGLONG)
#define MOD_IGNORE (MOD_TOPLEVEL | MOD_STORAGE | MOD_ADDRESSABLE | MOD_SIGNED | MOD_UNSIGNED)

static const char * type_difference(struct symbol *target, struct symbol *source,
	unsigned long target_mod_ignore, unsigned long source_mod_ignore)
{
	for (;;) {
		unsigned long mod1, mod2, diff;
		unsigned long as1, as2;

		if (target == source)
			break;
		if (!target || !source)
			return "different types";
		/*
		 * Peel of per-node information.
		 * FIXME! Check alignment, address space, and context too here!
		 */
		if (target->type == SYM_NODE)
			target = target->ctype.base_type;
		if (source->type == SYM_NODE)
			source = source->ctype.base_type;
		mod1 = target->ctype.modifiers;
		as1 = target->ctype.as;
		mod2 = source->ctype.modifiers;
		as2 = source->ctype.as;

		if (target->type != source->type) {
			int type1 = target->type;
			int type2 = source->type;

			/* Ignore ARRAY/PTR differences, as long as they point to the same type */
			type1 = type1 == SYM_ARRAY ? SYM_PTR : type1;
			type2 = type2 == SYM_ARRAY ? SYM_PTR : type2;
			if (type1 != type2)
				return "different base types";
		}

		/* Must be same address space to be comparable */
		if (as1 != as2)
			return "different address spaces";

		/* Ignore differences in storage types, sign, or addressability */
		diff = (mod1 ^ mod2) & ~MOD_IGNORE;
		if (diff) {
			mod1 &= diff & ~target_mod_ignore;
			mod2 &= diff & ~source_mod_ignore;
			if (mod1 | mod2) {
				if ((mod1 | mod2) & MOD_SIZE)
					return "different type sizes";
				return "different modifiers";
			}
		}

		target = target->ctype.base_type;
		source = source->ctype.base_type;
	}
	return NULL;
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

/*
 * Ignore differences in "volatile" and "const"ness when
 * subtracting pointers
 */
#define MOD_IGN (MOD_VOLATILE | MOD_CONST)

static struct symbol *evaluate_ptr_sub(struct expression *expr, struct expression *l, struct expression *r)
{
	const char *typediff;
	struct symbol *ctype;
	struct symbol *ltype = l->ctype, *rtype = r->ctype;

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, l, r);

	ctype = ltype;
	typediff = type_difference(ltype, rtype, MOD_IGN, MOD_IGN);
	if (typediff) {
		ctype = common_ptr_type(l, r);
		if (!ctype) {
			warn(expr->pos, "subtraction of different types can't work (%s)", typediff);
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
	struct expression *left = expr->left;
	struct expression *right;

	if (!evaluate_expression(left))
		return NULL;

	/* Do immediate short-circuiting ... */
	if (left->type == EXPR_VALUE) {
		if (expr->op == SPECIAL_LOGICAL_AND) {
			if (!left->value) {
				expr->type = EXPR_VALUE;
				expr->value = 0;
				expr->ctype = &bool_ctype;
				return &bool_ctype;
			}
		} else {
			if (left->value) {
				expr->type = EXPR_VALUE;
				expr->value = 1;
				expr->ctype = &bool_ctype;
				return &bool_ctype;
			}
		}
	}

	right = expr->right;
	if (!evaluate_expression(right))
		return NULL;
	expr->ctype = &bool_ctype;
	if (left->type == EXPR_VALUE && right->type == EXPR_VALUE) {
		/*
		 * We know the left value doesn't matter, since
		 * otherwise we would have short-circuited it..
		 */
		expr->type = EXPR_VALUE;
		expr->value = right->value;
	}

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
	struct symbol *ctype;

	/* Pointer types? */
	if (is_ptr_type(ltype) || is_ptr_type(rtype)) {
		expr->ctype = &bool_ctype;
		// FIXME! Check the types for compatibility
		return &bool_ctype;
	}

	ctype = compatible_integer_binop(expr, &expr->left, &expr->right);
	if (ctype) {
		simplify_int_binop(expr, ctype);
		expr->ctype = &bool_ctype;
		return &bool_ctype;
	}

	return bad_expr_type(expr);
}

static int compatible_integer_types(struct symbol *ltype, struct symbol *rtype)
{
	/* Integer promotion? */
	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
	if (ltype->type == SYM_ENUM || ltype->type == SYM_BITFIELD)
		ltype = &int_ctype;
	if (rtype->type == SYM_ENUM || rtype->type == SYM_BITFIELD)
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

static struct symbol *do_degenerate(struct expression **ep)
{
	struct expression *expr = *ep;
	return degenerate(expr, expr->ctype, ep);
}

static struct symbol * evaluate_conditional(struct expression *expr)
{
	struct expression *cond, *true, *false;
	struct symbol *ctype, *ltype, *rtype;
	const char * typediff;

	ctype = do_degenerate(&expr->conditional);
	cond = expr->conditional;

	ltype = ctype;
	true = cond;
	if (expr->cond_true) {
		ltype = do_degenerate(&expr->cond_true);
		true = expr->cond_true;
	}

	rtype = do_degenerate(&expr->cond_false);
	false = expr->cond_false;

	ctype = ltype;
	typediff = type_difference(ltype, rtype, MOD_IGN, MOD_IGN);
	if (typediff) {
		ctype = compatible_integer_binop(expr, &true, &expr->cond_false);
		if (!ctype) {
			ctype = compatible_ptr_type(true, expr->cond_false);
			if (!ctype) {
				warn(expr->pos, "incompatible types in conditional expression (%s)", typediff);
				return NULL;
			}
		}
	}

	/* Simplify conditional expression.. */
	if (cond->type == EXPR_VALUE) {
		if (!cond->value)
			true = false;
		*expr = *true;
	}
	expr->ctype = ctype;
	return ctype;
}
		
static int compatible_assignment_types(struct expression *expr, struct symbol *target,
	struct expression **rp, struct symbol *source, const char *where)
{
	const char *typediff;
	struct symbol *t;
	int target_as;

	/* It's ok if the target is more volatile or const than the source */
	typediff = type_difference(target, source, MOD_VOLATILE | MOD_CONST, 0);
	if (!typediff)
		return 1;

	if (compatible_integer_types(target, source)) {
		if (target->bit_size != source->bit_size)
			*rp = cast_to(*rp, target);
		return 1;
	}

	/* Pointer destination? */
	t = target;
	target_as = t->ctype.as;
	if (t->type == SYM_NODE) {
		t = t->ctype.base_type;
		target_as |= t->ctype.as;
	}
	if (t->type == SYM_PTR) {
		struct expression *right = *rp;
		struct symbol *s = source;
		int source_as;

		// NULL pointer is always ok
		if (right->type == EXPR_VALUE && !right->value)
			return 1;

		/* "void *" matches anything as long as the address space is ok */
		source_as = s->ctype.as;
		if (s->type == SYM_NODE) {
			s = s->ctype.base_type;
			source_as |= s->ctype.as;
		}
		if (source_as == target_as && (s->type == SYM_PTR || s->type == SYM_ARRAY)) {
			s = s->ctype.base_type;
			t = t->ctype.base_type;
			if (s == &void_ctype || t == &void_ctype)
				return 1;
		}

		if (s->type == SYM_FN) {
			typediff = type_difference(t->ctype.base_type, s, 0, 0);
			if (!typediff)
				return 1;
		}
	}

	// FIXME!! Cast it?
	warn(expr->pos, "incorrect type in %s (%s)", where, typediff);
	warn(expr->pos, "  expected %s", show_typename(target));
	warn(expr->pos, "  got %s", show_typename(source));
	return 0;
}

/*
 * FIXME!! This is wrong from a double evaluation standpoint. We can't
 * just expand the expression twice, that would make any side effects
 * happen twice too.
 */
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

	if (!lvalue_expression(left)) {
		warn(expr->pos, "not an lvalue");
		return NULL;
	}

	if (!compatible_assignment_types(expr, ltype, &expr->right, rtype, "assignment"))
		return 0;

	expr->ctype = expr->left->ctype;
	return expr->ctype;
}

static struct symbol *evaluate_addressof(struct expression *expr)
{
	struct symbol *ctype, *symbol;
	struct expression *op = expr->unop;

	if (op->op != '*' || op->type != EXPR_PREOP) {
		warn(expr->pos, "not addressable");
		return NULL;
	}

	symbol = alloc_symbol(expr->pos, SYM_PTR);
	symbol->ctype.alignment = POINTER_ALIGNMENT;
	symbol->bit_size = BITS_IN_POINTER;

	ctype = op->ctype;
	if (ctype->type == SYM_NODE) {
		ctype->ctype.modifiers |= MOD_ADDRESSABLE;
		if (ctype->ctype.modifiers & MOD_REGISTER) {
			warn(expr->pos, "taking address of 'register' variable '%s'", show_ident(ctype->ident));
			ctype->ctype.modifiers &= ~MOD_REGISTER;
		}
		symbol->ctype.modifiers = ctype->ctype.modifiers;
		symbol->ctype.as = ctype->ctype.as;
		symbol->ctype.context = ctype->ctype.context;
		symbol->ctype.contextmask = ctype->ctype.contextmask;
		ctype = ctype->ctype.base_type;
	}

	symbol->ctype.base_type = ctype;
	*expr = *op->unop;
	expr->ctype = symbol;
	return symbol;
}


static struct symbol *evaluate_dereference(struct expression *expr)
{
	struct expression *op = expr->unop;
	struct symbol *ctype = op->ctype, *sym;

	sym = alloc_symbol(expr->pos, SYM_NODE);
	if (ctype->type == SYM_NODE) {
		ctype = ctype->ctype.base_type;
		merge_type(sym, ctype);
	}
	sym->ctype = ctype->ctype;
	if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
		warn(expr->pos, "cannot derefence this type");
		return 0;
	}

	ctype = ctype->ctype.base_type;
	examine_symbol_type(ctype);
	if (!ctype) {
		warn(expr->pos, "undefined type");
		return NULL;
	}

	sym->bit_size = ctype->bit_size;
	sym->array_size = ctype->array_size;

	/* Simplify: *&(expr) => (expr) */
	if (op->type == EXPR_PREOP && op->op == '&') {
		*expr = *op->unop;
	}

	expr->ctype = sym;
	return sym;
}

static void simplify_preop(struct expression *expr)
{
	struct expression *op = expr->unop;
	unsigned long long v, mask;

	if (op->type != EXPR_VALUE)
		return;
	v = op->value;
	switch (expr->op) {
	case '+': break;
	case '-': v = -v; break;
	case '!': v = !v; break;
	case '~': v = ~v; break;
	default: return;
	}
	mask = 1ULL << (expr->ctype->bit_size-1);
	mask = mask | (mask-1);
	expr->value = v & mask;
	expr->type = EXPR_VALUE;
}

/*
 * Unary post-ops: x++ and x--
 */
static struct symbol *evaluate_postop(struct expression *expr)
{
	struct expression *op = expr->unop;
	struct symbol *ctype = op->ctype;

	if (!lvalue_expression(expr->unop)) {
		warn(expr->pos, "need lvalue expression for ++/--");
		return NULL;
	}
	expr->ctype = ctype;
	return ctype;
}

static struct symbol *evaluate_preop(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;

	switch (expr->op) {
	case '(':
	case '+':
		*expr = *expr->unop;
		return ctype;

	case '*':
		return evaluate_dereference(expr);

	case '&':
		return evaluate_addressof(expr);

	case SPECIAL_INCREMENT:
	case SPECIAL_DECREMENT:
		/*
		 * From a type evaluation standpoint the pre-ops are
		 * the same as the postops
		 */
		return evaluate_postop(expr);

	case '!':
		ctype = &bool_ctype;
		break;

	default:
		break;
	}
	expr->ctype = ctype;
	simplify_preop(expr);
	return &bool_ctype;
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

static struct expression *evaluate_offset(struct expression *expr, unsigned long offset)
{
	struct expression *add;

	if (!offset)
		return expr;

	/* Create a new add-expression */
	add = alloc_expression(expr->pos, EXPR_BINOP);
	add->op = '+';
	add->ctype = &ptr_ctype;
	add->left = expr;
	add->right = alloc_expression(expr->pos, EXPR_VALUE);
	add->right->ctype = &int_ctype;
	add->right->value = offset;

	simplify_int_binop(add, &ptr_ctype);
	return add;
}

/* structure/union dereference */
static struct symbol *evaluate_member_dereference(struct expression *expr)
{
	int offset;
	struct symbol *ctype, *member, *sym;
	struct expression *deref = expr->deref, *add;
	struct ident *ident = expr->member;
	unsigned int mod;
	int address_space;

	if (!evaluate_expression(deref))
		return NULL;
	if (!ident) {
		warn(expr->pos, "bad member name");
		return NULL;
	}

	ctype = deref->ctype;
	address_space = ctype->ctype.as;
	mod = ctype->ctype.modifiers;
	if (ctype->type == SYM_NODE) {
		ctype = ctype->ctype.base_type;
		address_space |= ctype->ctype.as;
		mod |= ctype->ctype.modifiers;
	}
	if (expr->op == SPECIAL_DEREFERENCE) {
		/* Arrays will degenerate into pointers for '->' */
		if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
			warn(expr->pos, "expected a pointer to a struct/union");
			return NULL;
		}
		mod = ctype->ctype.modifiers;
		address_space = ctype->ctype.as;
		ctype = ctype->ctype.base_type;
		if (ctype->type == SYM_NODE) {
			mod |= ctype->ctype.modifiers;
			address_space |= ctype->ctype.as;
			ctype = ctype->ctype.base_type;
		}
	} else {
		if (!lvalue_expression(deref)) {
			warn(deref->pos, "expected lvalue for member dereference");
			return NULL;
		}
		deref = deref->unop;
		expr->deref = deref;
	}
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

	add = evaluate_offset(deref, offset);

	sym = alloc_symbol(expr->pos, SYM_NODE);
	sym->bit_size = member->bit_size;
	sym->array_size = member->array_size;
	sym->ctype = member->ctype;
	sym->ctype.modifiers = mod;
	sym->ctype.as = address_space;
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

	expr->ctype = sym;
	return sym;
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

static int evaluate_arguments(struct symbol *fn, struct expression_list *head)
{
	struct expression *expr;
	struct symbol_list *argument_types = fn->arguments;
	struct symbol *argtype;
	int i = 1;

	PREPARE_PTR_LIST(argument_types, argtype);
	FOR_EACH_PTR (head, expr) {
		struct expression **p = THIS_ADDRESS(expr);
		struct symbol *ctype, *target;
		ctype = evaluate_expression(expr);

		if (!ctype)
			return 0;

		ctype = degenerate(expr, ctype, p);

		target = argtype;
		if (!target && ctype->bit_size < BITS_IN_INT)
			target = &int_ctype;
		if (target) {
			static char where[30];
			examine_symbol_type(target);
			sprintf(where, "argument %d", i);
			compatible_assignment_types(expr, target, p, ctype, where);
		}

		i++;
		NEXT_PTR_LIST(argument_types, argtype);
	} END_FOR_EACH_PTR;
	FINISH_PTR_LIST;
	return 1;
}

static int evaluate_array_initializer(struct symbol *ctype, struct expression *expr)
{
	struct expression *entry;
	int current = 0;
	int max = 0;

	FOR_EACH_PTR(expr->expr_list, entry) {
		struct expression **p = THIS_ADDRESS(entry);
		struct symbol *rtype;

		if (entry->type == EXPR_INDEX) {
			current = entry->idx_to;
			continue;
		}
		rtype = evaluate_expression(entry);
		if (!rtype)
			continue;
		compatible_assignment_types(entry, ctype, p, rtype, "array initializer");
		current++;
		if (current > max)
			max = current;
	} END_FOR_EACH_PTR;
	return max;
}

static int evaluate_struct_or_union_initializer(struct symbol *ctype, struct expression *expr, int multiple)
{
	/* Fixme: walk through the struct/union definitions and try to assign right types! */
	return 0;
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
		int size = 0;
		struct symbol *rtype = evaluate_expression(expr);
		if (rtype) {
			compatible_assignment_types(expr, ctype, ep, rtype, "initializer");
			/* strings are special: char arrays */
			if (rtype->type == SYM_ARRAY)
				size = rtype->array_size;
		}
		return size;
	}

	expr->ctype = ctype;
	if (ctype->type = SYM_NODE)
		ctype = ctype->ctype.base_type;

	switch (ctype->type) {
	case SYM_ARRAY:
	case SYM_PTR:
		return evaluate_array_initializer(ctype->ctype.base_type, expr);
	case SYM_UNION:
		return evaluate_struct_or_union_initializer(ctype, expr, 0);
	case SYM_STRUCT:
		return evaluate_struct_or_union_initializer(ctype, expr, 1);
	default:
		break;
	}
	warn(expr->pos, "unexpected compound initializer");
	return 0;
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
	ctype = fn->ctype;
	if (ctype->type == SYM_NODE) {
		/*
		 * FIXME!! We should really expand the inline functions.
		 * For now we just mark them accessed so that they show
		 * up on the list of used symbols.
		 */
		if (ctype->ctype.modifiers & MOD_INLINE)
			access_symbol(ctype);
		ctype = ctype->ctype.base_type;
	}
	if (ctype->type == SYM_PTR || ctype->type == SYM_ARRAY)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_FN) {
		warn(expr->pos, "not a function");
		return NULL;
	}
	if (!evaluate_arguments(ctype, arglist))
		return NULL;
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
	case EXPR_LOGICAL:
		return evaluate_logical(expr);
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
		if (!evaluate_expression(expr->left))
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
		return evaluate_member_dereference(expr);
	case EXPR_CALL:
		return evaluate_call(expr);
	case EXPR_BITFIELD:
		warn(expr->pos, "bitfield generated by parser");
		return NULL;
	case EXPR_CONDITIONAL:
		if (!evaluate_expression(expr->conditional))
			return NULL;
		if (!evaluate_expression(expr->cond_false))
			return NULL;
		if (expr->cond_true && !evaluate_expression(expr->cond_true))
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

static void check_duplicates(struct symbol *sym)
{
	struct symbol *next = sym;

	while ((next = next->same_symbol) != NULL) {
		const char *typediff;
		evaluate_symbol(next);
		typediff = type_difference(sym, next, 0, 0);
		if (typediff) {
			warn(sym->pos, "symbol '%s' redeclared with different type (originally declared at %s:%d)",
				show_ident(sym->ident),
				input_streams[next->pos.stream].name, next->pos.line);
			return;
		}
	}
}

struct symbol *evaluate_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	sym = examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return NULL;

	check_duplicates(sym);

	/* Evaluate the initializers */
	if (sym->initializer) {
		int count = evaluate_initializer(sym, &sym->initializer);
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
			current_fn = base_type;
			current_contextmask = sym->ctype.contextmask;
			current_context = sym->ctype.context;
			evaluate_statement(base_type->stmt);
		}
	}

	return base_type;
}

struct symbol *evaluate_return_expression(struct statement *stmt)
{
	struct expression *expr = stmt->expression;
	struct symbol *ctype, *fntype;

	fntype = current_fn->ctype.base_type;
	if (fntype == &void_ctype) {
		if (expr)
			warn(expr->pos, "return expression in void function");
		return NULL;
	}

	if (!expr) {
		warn(stmt->pos, "return with no return value");
		return NULL;
	}
	ctype = evaluate_expression(expr);
	if (!ctype)
		return NULL;
	ctype = degenerate(expr, ctype, &expr);
	expr->ctype = ctype;
	compatible_assignment_types(expr, fntype, &expr, ctype, "return expression");
	stmt->expression = expr;
	return NULL;
}

struct symbol *evaluate_statement(struct statement *stmt)
{
	if (!stmt)
		return NULL;

	switch (stmt->type) {
	case STMT_RETURN:
		return evaluate_return_expression(stmt);

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
		break;
	case STMT_ASM:
		/* FIXME! Do the asm parameter evaluation! */
		break;
	}
	return NULL;
}

long long get_expression_value(struct expression *expr)
{
	long long value, mask;
	struct symbol *ctype;

	ctype = evaluate_expression(expr);
	if (!ctype || expr->type != EXPR_VALUE) {
		warn(expr->pos, "bad constant expression");
		return 0;
	}

	value = expr->value;
	mask = 1ULL << (ctype->bit_size-1);

	if (value & mask) {
		while (ctype->type != SYM_BASETYPE)
			ctype = ctype->ctype.base_type;
		if (!(ctype->ctype.modifiers & MOD_UNSIGNED))
			value = value | mask | ~(mask-1);
	}
	return value;
}
