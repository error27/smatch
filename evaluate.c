/*
 * sparse/evaluate.c
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
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

struct symbol *current_fn;

static struct symbol *degenerate(struct expression *expr);

static struct symbol *evaluate_symbol_expression(struct expression *expr)
{
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		warning(expr->pos, "undefined identifier '%s'", show_ident(expr->symbol_name));
		return NULL;
	}

	examine_symbol_type(sym);

	base_type = sym->ctype.base_type;
	if (!base_type) {
		warning(expr->pos, "identifier '%s' has no type", show_ident(expr->symbol_name));
		return NULL;
	}

	/* The type of a symbol is the symbol itself! */
	expr->ctype = sym;

	/* enums can be turned into plain values */
	if (sym->type != SYM_ENUM) {
		struct expression *addr = alloc_expression(expr->pos, EXPR_SYMBOL);
		addr->symbol = sym;
		addr->symbol_name = expr->symbol_name;
		addr->ctype = &lazy_ptr_ctype;	/* Lazy evaluation: we need to do a proper job if somebody does &sym */
		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->unop = addr;
		return sym;
	} else if (base_type->bit_size < bits_in_int) {
		/* ugly - we need to force sizeof for these guys */
		struct expression *e = alloc_expression(expr->pos, EXPR_VALUE);
		e->value = sym->value;
		e->ctype = base_type;
		expr->type = EXPR_PREOP;
		expr->op = '+';
		expr->unop = e;
	} else {
		expr->type = EXPR_VALUE;
		expr->value = sym->value;
	}
	expr->ctype = base_type;
	return base_type;
}

static struct symbol *evaluate_string(struct expression *expr)
{
	struct symbol *sym = alloc_symbol(expr->pos, SYM_NODE);
	struct symbol *array = alloc_symbol(expr->pos, SYM_ARRAY);
	struct expression *addr = alloc_expression(expr->pos, EXPR_SYMBOL);
	struct expression *initstr = alloc_expression(expr->pos, EXPR_STRING);
	unsigned int length = expr->string->length;

	sym->array_size = alloc_const_expression(expr->pos, length);
	sym->bit_size = bits_in_char * length;
	sym->ctype.alignment = 1;
	sym->ctype.modifiers = MOD_STATIC;
	sym->ctype.base_type = array;
	sym->initializer = initstr;

	initstr->ctype = sym;
	initstr->string = expr->string;

	array->array_size = sym->array_size;
	array->bit_size = bits_in_char * length;
	array->ctype.alignment = 1;
	array->ctype.modifiers = MOD_STATIC;
	array->ctype.base_type = &char_ctype;
	
	addr->symbol = sym;
	addr->ctype = &lazy_ptr_ctype;

	expr->type = EXPR_PREOP;
	expr->op = '*';
	expr->unop = addr;  
	expr->ctype = sym;
	return sym;
}

static inline struct symbol *integer_promotion(struct symbol *type)
{
	unsigned long mod =  type->ctype.modifiers;
	int width;

	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type->type == SYM_ENUM)
		type = type->ctype.base_type;
	width = type->bit_size;
	if (type->type == SYM_BITFIELD)
		type = type->ctype.base_type;
	mod = type->ctype.modifiers;
	if (width < bits_in_int)
		return &int_ctype;

	/* If char/short has as many bits as int, it still gets "promoted" */
	if (mod & (MOD_CHAR | MOD_SHORT)) {
		type = &int_ctype;
		if (mod & MOD_UNSIGNED)
			type = &uint_ctype;
	}
	return type;
}

/*
 * integer part of usual arithmetic conversions:
 *	integer promotions are applied
 *	if left and right are identical, we are done
 *	if signedness is the same, convert one with lower rank
 *	unless unsigned argument has rank lower than signed one, convert the
 *	signed one.
 *	if signed argument is bigger than unsigned one, convert the unsigned.
 *	otherwise, convert signed.
 *
 * Leaving aside the integer promotions, that is equivalent to
 *	if identical, don't convert
 *	if left is bigger than right, convert right
 *	if right is bigger than left, convert right
 *	otherwise, if signedness is the same, convert one with lower rank
 *	otherwise convert the signed one.
 */
static struct symbol *bigger_int_type(struct symbol *left, struct symbol *right)
{
	unsigned long lmod, rmod;

	left = integer_promotion(left);
	right = integer_promotion(right);

	if (left == right)
		goto left;

	if (left->bit_size > right->bit_size)
		goto left;

	if (right->bit_size > left->bit_size)
		goto right;

	lmod = left->ctype.modifiers;
	rmod = right->ctype.modifiers;
	if ((lmod ^ rmod) & MOD_UNSIGNED) {
		if (lmod & MOD_UNSIGNED)
			goto left;
	} else if ((lmod & ~rmod) & (MOD_LONG | MOD_LONGLONG))
		goto left;
right:
	left = right;
left:
	return left;
}

static struct expression * cast_to(struct expression *old, struct symbol *type)
{
	struct expression *expr = alloc_expression(old->pos, EXPR_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	return expr;
}

static int is_type_type(struct symbol *type)
{
	return (type->ctype.modifiers & MOD_TYPE) != 0;
}

static int is_ptr_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->type == SYM_PTR || type->type == SYM_ARRAY || type->type == SYM_FN;
}

static inline int is_float_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->ctype.base_type == &fp_type;
}

static inline int is_byte_type(struct symbol *type)
{
	return type->bit_size == bits_in_char && type->type != SYM_BITFIELD;
}

static inline int is_string_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->type == SYM_ARRAY && is_byte_type(type->ctype.base_type);
}

static struct symbol *bad_expr_type(struct expression *expr)
{
	warning(expr->pos, "incompatible types for operation (%s)", show_special(expr->op));
	switch (expr->type) {
	case EXPR_BINOP:
	case EXPR_COMPARE:
		info(expr->pos, "   left side has type %s", show_typename(expr->left->ctype));
		info(expr->pos, "   right side has type %s", show_typename(expr->right->ctype));
		break;
	case EXPR_PREOP:
	case EXPR_POSTOP:
		info(expr->pos, "   argument has type %s", show_typename(expr->unop->ctype));
		break;
	default:
		break;
	}

	return NULL;
}

static struct symbol *compatible_float_binop(struct expression **lp, struct expression **rp)
{
	struct expression *left = *lp, *right = *rp;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
	if (is_float_type(ltype)) {
		if (is_int_type(rtype))
			goto Left;
		if (is_float_type(rtype)) {
			unsigned long lmod = ltype->ctype.modifiers;
			unsigned long rmod = rtype->ctype.modifiers;
			lmod &= MOD_LONG | MOD_LONGLONG;
			rmod &= MOD_LONG | MOD_LONGLONG;
			if (lmod == rmod)
				return ltype;
			if (lmod & ~rmod)
				goto Left;
			else
				goto Right;
		}
		return NULL;
	}
	if (!is_float_type(rtype) || !is_int_type(ltype))
		return NULL;
Right:
	*lp = cast_to(left, rtype);
	return rtype;
Left:
	*rp = cast_to(right, ltype);
	return ltype;
}

static struct symbol *compatible_integer_binop(struct expression **lp, struct expression **rp)
{
	struct expression *left = *lp, *right = *rp;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
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

static int restricted_value(struct expression *v, struct symbol *type)
{
	if (v->type != EXPR_VALUE)
		return 1;
	if (v->value != 0)
		return 1;
	return 0;
}

static int restricted_binop(int op, struct symbol *type)
{
	switch (op) {
		case '&':
		case '|':
		case '^':
		case '?':
		case SPECIAL_EQUAL:
		case SPECIAL_NOTEQUAL:
			return 0;
		default:
			return 1;
	}
}

static int restricted_unop(int op, struct symbol *type)
{
	if (op == '~' && type->bit_size >= bits_in_int)
		return 0;
	if (op == '+')
		return 0;
	return 1;
}

static struct symbol *compatible_restricted_binop(int op, struct expression **lp, struct expression **rp)
{
	struct expression *left = *lp, *right = *rp;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;
	struct symbol *type = NULL;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (ltype->type == SYM_ENUM)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
	if (rtype->type == SYM_ENUM)
		rtype = rtype->ctype.base_type;
	if (is_restricted_type(ltype)) {
		if (is_restricted_type(rtype)) {
			if (ltype == rtype)
				type = ltype;
		} else {
			if (!restricted_value(right, ltype))
				type = ltype;
		}
	} else if (is_restricted_type(rtype)) {
		if (!restricted_value(left, rtype))
			type = rtype;
	}
	if (!type)
		return NULL;
	if (restricted_binop(op, type))
		return NULL;
	return type;
}

static struct symbol *evaluate_arith(struct expression *expr, int float_ok)
{
	struct symbol *ctype = compatible_integer_binop(&expr->left, &expr->right);
	if (!ctype && float_ok)
		ctype = compatible_float_binop(&expr->left, &expr->right);
	if (!ctype)
		ctype = compatible_restricted_binop(expr->op, &expr->left, &expr->right);
	if (ctype) {
		expr->ctype = ctype;
		return ctype;
	}
	return bad_expr_type(expr);
}

static inline int lvalue_expression(struct expression *expr)
{
	return (expr->type == EXPR_PREOP && expr->op == '*') || expr->type == EXPR_BITFIELD;
}

static struct symbol *evaluate_ptr_add(struct expression *expr, struct expression *ptr, struct expression *i)
{
	struct symbol *ctype;
	struct symbol *ptr_type = ptr->ctype;
	int bit_size;

	if (ptr_type->type == SYM_NODE)
		ptr_type = ptr_type->ctype.base_type;

	if (!is_int_type(i->ctype))
		return bad_expr_type(expr);

	ctype = ptr->ctype;
	examine_symbol_type(ctype);

	ctype = degenerate(ptr);
	if (!ctype->ctype.base_type) {
		warning(expr->pos, "missing type information");
		return NULL;
	}

	/* Get the size of whatever the pointer points to */
	ptr_type = ctype;
	if (ptr_type->type == SYM_NODE)
	ptr_type = ptr_type->ctype.base_type;
	if (ptr_type->type == SYM_PTR)
		ptr_type = ptr_type->ctype.base_type;
	bit_size = ptr_type->bit_size;

	/* Special case: adding zero commonly happens as a result of 'array[0]' */
	if (i->type == EXPR_VALUE && !i->value) {
		*expr = *ptr;
	} else if (bit_size > bits_in_char) {
		struct expression *add = expr;
		struct expression *mul = alloc_expression(expr->pos, EXPR_BINOP);
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);

		val->ctype = size_t_ctype;
		val->value = bit_size >> 3;

		mul->op = '*';
		mul->ctype = size_t_ctype;
		mul->left = i;
		mul->right = val;

		/* Leave 'add->op' as 'expr->op' - either '+' or '-' */
		add->left = ptr;
		add->right = mul;
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
		
	return evaluate_arith(expr, 1);
}

#define MOD_SIZE (MOD_CHAR | MOD_SHORT | MOD_LONG | MOD_LONGLONG)
#define MOD_IGNORE (MOD_TOPLEVEL | MOD_STORAGE | MOD_ADDRESSABLE |	\
	MOD_ASSIGNED | MOD_USERTYPE | MOD_FORCE | MOD_ACCESSED | MOD_EXPLICITLY_SIGNED)

const char * type_difference(struct symbol *target, struct symbol *source,
	unsigned long target_mod_ignore, unsigned long source_mod_ignore)
{
	for (;;) {
		unsigned long mod1, mod2, diff;
		unsigned long as1, as2;
		int type1, type2;
		struct symbol *base1, *base2;

		if (target == source)
			break;
		if (!target || !source)
			return "different types";
		/*
		 * Peel of per-node information.
		 * FIXME! Check alignment and context too here!
		 */
		mod1 = target->ctype.modifiers;
		as1 = target->ctype.as;
		mod2 = source->ctype.modifiers;
		as2 = source->ctype.as; 
		if (target->type == SYM_NODE) {
			target = target->ctype.base_type;
			if (!target)
				return "bad types";
			if (target->type == SYM_PTR) {
				mod1 = 0;
				as1 = 0;
			}	
			mod1 |= target->ctype.modifiers;
			as1 |= target->ctype.as;
		}
		if (source->type == SYM_NODE) {
			source = source->ctype.base_type;
			if (!source)
				return "bad types";
			if (source->type == SYM_PTR) {
				mod2 = 0;
				as2 = 0;
			}
			mod2 |= source->ctype.modifiers;
			as2 |= source->ctype.as; 
		}
		if (target->type == SYM_ENUM) {
			target = target->ctype.base_type;
			if (!target)
				return "bad types";
		}
		if (source->type == SYM_ENUM) {
			source = source->ctype.base_type;
			if (!source)
				return "bad types";
		}

		if (target == source)
			break;
		if (!target || !source)
			return "different types";

		type1 = target->type;
		base1 = target->ctype.base_type;

		type2 = source->type;
		base2 = source->ctype.base_type;

		/*
		 * Pointers to functions compare as the function itself
		 */
		if (type1 == SYM_PTR && base1) {
			switch (base1->type) {
			case SYM_FN:
				type1 = SYM_FN;
				target = base1;
				base1 = base1->ctype.base_type;
			default:
				/* nothing */;
			}
		}
		if (type2 == SYM_PTR && base2) {
			switch (base2->type) {
			case SYM_FN:
				type2 = SYM_FN;
				source = base2;
				base2 = base2->ctype.base_type;
			default:
				/* nothing */;
			}
		}

		/* Arrays degenerate to pointers for type comparisons */
		type1 = (type1 == SYM_ARRAY) ? SYM_PTR : type1;
		type2 = (type2 == SYM_ARRAY) ? SYM_PTR : type2;

		if (type1 != type2 || type1 == SYM_RESTRICT)
			return "different base types";

		/* Must be same address space to be comparable */
		if (as1 != as2)
			return "different address spaces";

		/* Ignore differences in storage types or addressability */
		diff = (mod1 ^ mod2) & ~MOD_IGNORE;
		diff &= (mod1 & ~target_mod_ignore) | (mod2 & ~source_mod_ignore);
		if (diff) {
			if (diff & MOD_SIZE)
				return "different type sizes";
			if (diff & ~MOD_SIGNEDNESS)
				return "different modifiers";

			/* Differs in signedness only.. */
			if (Wtypesign) {
				/*
				 * Warn if both are explicitly signed ("unsigned" is obvously
				 * always explicit, and since we know one of them has to be
				 * unsigned, we check if the signed one was explicit).
				 */
				if ((mod1 | mod2) & MOD_EXPLICITLY_SIGNED)
					return "different explicit signedness";

				/*
				 * "char" matches both "unsigned char" and "signed char",
				 * so if the explicit test didn't trigger, then we should
				 * not warn about a char.
				 */
				if (!(mod1 & MOD_CHAR))
					return "different signedness";
			}
		}

		if (type1 == SYM_FN) {
			int i;
			struct symbol *arg1, *arg2;
			if (base1->variadic != base2->variadic)
				return "incompatible variadic arguments";
			PREPARE_PTR_LIST(target->arguments, arg1);
			PREPARE_PTR_LIST(source->arguments, arg2);
			i = 1;
			for (;;) {
				const char *diff;
				diff = type_difference(arg1, arg2, 0, 0);
				if (diff) {
					static char argdiff[80];
					sprintf(argdiff, "incompatible argument %d (%s)", i, diff);
					return argdiff;
				}
				if (!arg1)
					break;
				NEXT_PTR_LIST(arg1);
				NEXT_PTR_LIST(arg2);
				i++;
			}
			FINISH_PTR_LIST(arg2);
			FINISH_PTR_LIST(arg1);
		}

		target = base1;
		source = base2;
	}
	return NULL;
}

static int is_null_ptr(struct expression *expr)
{
	if (expr->type != EXPR_VALUE || expr->value)
		return 0;
	if (!is_ptr_type(expr->ctype))
		warning(expr->pos, "Using plain integer as NULL pointer");
	return 1;
}

static struct symbol *common_ptr_type(struct expression *l, struct expression *r)
{
	/* NULL expression? Just return the type of the "other side" */
	if (is_null_ptr(r))
		return l->ctype;
	if (is_null_ptr(l))
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
	struct symbol *ltype, *rtype;

	ltype = degenerate(l);
	rtype = degenerate(r);

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, l, r);

	ctype = ltype;
	typediff = type_difference(ltype, rtype, ~MOD_SIZE, ~MOD_SIZE);
	if (typediff) {
		ctype = common_ptr_type(l, r);
		if (!ctype) {
			warning(expr->pos, "subtraction of different types can't work (%s)", typediff);
			return NULL;
		}
	}
	examine_symbol_type(ctype);

	/* Figure out the base type we point to */
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
		warning(expr->pos, "subtraction of functions? Share your drugs");
		return NULL;
	}
	ctype = ctype->ctype.base_type;

	expr->ctype = ssize_t_ctype;
	if (ctype->bit_size > bits_in_char) {
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

	return evaluate_arith(expr, 1);
}

#define is_safe_type(type) ((type)->ctype.modifiers & MOD_SAFE)

static struct symbol *evaluate_conditional(struct expression *expr)
{
	struct symbol *ctype;

	if (!expr)
		return NULL;

	if (expr->type == EXPR_ASSIGNMENT)
		warning(expr->pos, "assignment expression in conditional");

	ctype = evaluate_expression(expr);
	if (ctype) {
		if (is_safe_type(ctype))
			warning(expr->pos, "testing a 'safe expression'");
	}

	return ctype;
}

static struct symbol *evaluate_logical(struct expression *expr)
{
	if (!evaluate_conditional(expr->left))
		return NULL;
	if (!evaluate_conditional(expr->right))
		return NULL;

	expr->ctype = &bool_ctype;
	return &bool_ctype;
}

static struct symbol *evaluate_shift(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;
	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;
	if (is_int_type(ltype) && is_int_type(rtype)) {
		struct symbol *ctype = integer_promotion(ltype);
		if (ltype->bit_size != ctype->bit_size)
			expr->left = cast_to(expr->left, ctype);
		expr->ctype = ctype;
		ctype = integer_promotion(rtype);
		if (rtype->bit_size != ctype->bit_size)
			expr->right = cast_to(expr->right, ctype);
		return expr->ctype;
	}
	return bad_expr_type(expr);
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
	case '*': case '/':
		return evaluate_arith(expr, 1);

	// shifts do integer promotions, but that's it.
	case SPECIAL_LEFTSHIFT: case SPECIAL_RIGHTSHIFT:
		return evaluate_shift(expr);

	// The rest are integer operations
	// '%', '&', '^', '|'
	default:
		return evaluate_arith(expr, 0);
	}
}

static struct symbol *evaluate_comma(struct expression *expr)
{
	expr->ctype = expr->right->ctype;
	return expr->ctype;
}

static int modify_for_unsigned(int op)
{
	if (op == '<')
		op = SPECIAL_UNSIGNED_LT;
	else if (op == '>')
		op = SPECIAL_UNSIGNED_GT;
	else if (op == SPECIAL_LTE)
		op = SPECIAL_UNSIGNED_LTE;
	else if (op == SPECIAL_GTE)
		op = SPECIAL_UNSIGNED_GTE;
	return op;
}

static struct symbol *evaluate_compare(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;
	struct symbol *ctype;

	/* Type types? */
	if (is_type_type(ltype) && is_type_type(rtype))
		goto OK;

	if (is_safe_type(ltype) || is_safe_type(rtype))
		warning(expr->pos, "testing a 'safe expression'");

	/* Pointer types? */
	if (is_ptr_type(ltype) || is_ptr_type(rtype)) {
		// FIXME! Check the types for compatibility
		goto OK;
	}

	ctype = compatible_integer_binop(&expr->left, &expr->right);
	if (ctype) {
		if (ctype->ctype.modifiers & MOD_UNSIGNED)
			expr->op = modify_for_unsigned(expr->op);
		goto OK;
	}

	ctype = compatible_float_binop(&expr->left, &expr->right);
	if (ctype)
		goto OK;

	ctype = compatible_restricted_binop(expr->op, &expr->left, &expr->right);
	if (ctype)
		goto OK;

	bad_expr_type(expr);

OK:
	expr->ctype = &bool_ctype;
	return &bool_ctype;
}

/*
 * FIXME!! This should do casts, array degeneration etc..
 */
static struct symbol *compatible_ptr_type(struct expression *left, struct expression *right)
{
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (ltype->type == SYM_NODE)
		ltype = ltype->ctype.base_type;

	if (rtype->type == SYM_NODE)
		rtype = rtype->ctype.base_type;

	if (ltype->type == SYM_PTR) {
		if (is_null_ptr(right) || rtype->ctype.base_type == &void_ctype)
			return ltype;
	}

	if (rtype->type == SYM_PTR) {
		if (is_null_ptr(left) || ltype->ctype.base_type == &void_ctype)
			return rtype;
	}
	return NULL;
}

static struct symbol *evaluate_conditional_expression(struct expression *expr)
{
	struct symbol *ctype, *ltype, *rtype;
	const char * typediff;

	if (!evaluate_conditional(expr->conditional))
		return NULL;
	if (!evaluate_expression(expr->cond_false))
		return NULL;
	if (!evaluate_expression(expr->cond_true))
		return NULL;

	ctype = degenerate(expr->conditional);

	ltype = degenerate(expr->cond_true);
	rtype = degenerate(expr->cond_false);

	ctype = ltype;
	typediff = type_difference(ltype, rtype, MOD_IGN, MOD_IGN);
	if (!typediff)
		goto out;

	ctype = compatible_integer_binop(&expr->cond_true, &expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_ptr_type(expr->cond_true, expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_float_binop(&expr->cond_true, &expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_restricted_binop('?', &expr->cond_true, &expr->cond_false);
	if (ctype)
		goto out;
	warning(expr->pos, "incompatible types in conditional expression (%s)", typediff);
	return NULL;

out:
	expr->ctype = ctype;
	return ctype;
}

static struct symbol *evaluate_short_conditional(struct expression *expr)
{
	struct symbol *a = alloc_symbol(expr->pos, SYM_NODE);
	struct expression *e0, *e1, *e2, *e3;
	struct symbol *ctype;

	if (!evaluate_expression(expr->conditional))
		return NULL;

	ctype = degenerate(expr->conditional);

	a->ctype.base_type = ctype;
	a->bit_size = ctype->bit_size;
	a->array_size = ctype->array_size;

	e0 = alloc_expression(expr->pos, EXPR_SYMBOL);
	e0->symbol = a;
	e0->ctype = &lazy_ptr_ctype;

	e1 = alloc_expression(expr->pos, EXPR_PREOP);
	e1->unop = e0;
	e1->op = '*';
	e1->ctype = ctype;

	e2 = alloc_expression(expr->pos, EXPR_ASSIGNMENT);
	e2->left = e1;
	e2->right = expr->conditional;
	e2->op = '=';
	e2->ctype = ctype;

	e3 = alloc_expression(expr->pos, EXPR_CONDITIONAL);
	e3->conditional = e1;
	e3->cond_true = e1;
	e3->cond_false = expr->cond_false;
	e3->ctype = evaluate_conditional_expression(e3);

	expr->type = EXPR_COMMA;
	expr->left = e2;
	expr->right = e3;
	return expr->ctype = e3->ctype;
}
		
static int compatible_assignment_types(struct expression *expr, struct symbol *target,
	struct expression **rp, struct symbol *source, const char *where)
{
	const char *typediff;
	struct symbol *t;
	int target_as;

	if (is_int_type(target)) {
		if (is_int_type(source)) {
			if (target->bit_size != source->bit_size)
				goto Cast;
			if (target->bit_offset != source->bit_offset)
				goto Cast;
			return 1;
		}
		if (is_float_type(source))
			goto Cast;
	} else if (is_float_type(target)) {
		if (is_int_type(source))
			goto Cast;
		if (is_float_type(source)) {
			if (target->bit_size != source->bit_size)
				goto Cast;
			return 1;
		}
	}

	/* It's ok if the target is more volatile or const than the source */
	typediff = type_difference(target, source, MOD_VOLATILE | MOD_CONST, 0);
	if (!typediff)
		return 1;

	if (is_restricted_type(target) && !restricted_value(*rp, target))
		return 1;

	/* Pointer destination? */
	t = target;
	target_as = t->ctype.as;
	if (t->type == SYM_NODE) {
		t = t->ctype.base_type;
		target_as |= t->ctype.as;
	}
	if (t->type == SYM_PTR || t->type == SYM_FN || t->type == SYM_ARRAY) {
		struct expression *right = *rp;
		struct symbol *s = source;
		int source_as;

		// NULL pointer is always ok
		if (is_null_ptr(right))
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
	}

	warning(expr->pos, "incorrect type in %s (%s)", where, typediff);
	info(expr->pos, "   expected %s", show_typename(target));
	info(expr->pos, "   got %s", show_typename(source));
	*rp = cast_to(*rp, target);
	return 0;
Cast:
	*rp = cast_to(*rp, target);
	return 1;
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
		[SPECIAL_OR_ASSIGN - SPECIAL_BASE] = '|',
		[SPECIAL_XOR_ASSIGN - SPECIAL_BASE] = '^'
	};
	struct expression *e0, *e1, *e2, *e3, *e4, *e5;
	struct symbol *a = alloc_symbol(expr->pos, SYM_NODE);
	struct symbol *ltype = left->ctype;
	struct expression *addr;
	struct symbol *lptype;

	if (left->type == EXPR_BITFIELD)
		addr = left->address;
	else
		addr = left->unop;

	lptype = addr->ctype;

	a->ctype.base_type = lptype;
	a->bit_size = lptype->bit_size;
	a->array_size = lptype->array_size;

	e0 = alloc_expression(expr->pos, EXPR_SYMBOL);
	e0->symbol = a;
	e0->ctype = &lazy_ptr_ctype;

	e1 = alloc_expression(expr->pos, EXPR_PREOP);
	e1->unop = e0;
	e1->op = '*';
	e1->ctype = lptype;

	e2 = alloc_expression(expr->pos, EXPR_ASSIGNMENT);
	e2->left = e1;
	e2->right = addr;
	e2->op = '=';
	e2->ctype = lptype;

	/* we can't cannibalize left, unfortunately */
	e3 = alloc_expression(expr->pos, left->type);
	*e3 = *left;
	if (e3->type == EXPR_BITFIELD)
		e3->address = e1;
	else
		e3->unop = e1;

	e4 = alloc_expression(expr->pos, EXPR_BINOP);
	e4->op = subexpr->op = op_trans[op - SPECIAL_BASE];
	e4->left = e3;
	e4->right = right;
	/* will calculate type later */

	e5 = alloc_expression(expr->pos, EXPR_ASSIGNMENT);
	e5->left = e3;	/* we can share that one */
	e5->right = e4;
	e5->op = '=';
	e5->ctype = ltype;

	expr->type = EXPR_COMMA;
	expr->left = e2;
	expr->right = e5;
	expr->ctype = ltype;

	return evaluate_binop(e4);
}

static void evaluate_assign_to(struct expression *left, struct symbol *type)
{
	if (type->ctype.modifiers & MOD_CONST)
		warning(left->pos, "assignment to const expression");
	if (type->type == SYM_NODE)
		type->ctype.modifiers |= MOD_ASSIGNED;
}

static struct symbol *evaluate_assignment(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct expression *where = expr;
	struct symbol *ltype, *rtype;

	if (!lvalue_expression(left)) {
		warning(expr->pos, "not an lvalue");
		return NULL;
	}

	ltype = left->ctype;

	if (expr->op != '=') {
		if (!evaluate_binop_assignment(expr, left, right))
			return NULL;
		where = expr->right;	/* expr is EXPR_COMMA now */
		left = where->left;
		right = where->right;
	}

	rtype = degenerate(right);

	if (!compatible_assignment_types(where, ltype, &where->right, rtype, "assignment"))
		return NULL;

	evaluate_assign_to(left, ltype);

	expr->ctype = ltype;
	return ltype;
}

static void examine_fn_arguments(struct symbol *fn)
{
	struct symbol *s;

	FOR_EACH_PTR(fn->arguments, s) {
		struct symbol *arg = evaluate_symbol(s);
		/* Array/function arguments silently degenerate into pointers */
		if (arg) {
			struct symbol *ptr;
			switch(arg->type) {
			case SYM_ARRAY:
			case SYM_FN:
				ptr = alloc_symbol(s->pos, SYM_PTR);
				if (arg->type == SYM_ARRAY)
					ptr->ctype = arg->ctype;
				else
					ptr->ctype.base_type = arg;
				ptr->ctype.as |= s->ctype.as;
				ptr->ctype.modifiers |= s->ctype.modifiers;

				s->ctype.base_type = ptr;
				s->ctype.as = 0;
				s->ctype.modifiers = 0;
				s->bit_size = 0;
				examine_symbol_type(s);
				break;
			default:
				/* nothing */
				break;
			}
		}
	} END_FOR_EACH_PTR(s);
}

static struct symbol *convert_to_as_mod(struct symbol *sym, int as, int mod)
{
	if (sym->ctype.as != as || sym->ctype.modifiers != mod) {
		struct symbol *newsym = alloc_symbol(sym->pos, SYM_NODE);
		*newsym = *sym;
		newsym->ctype.as = as;
		newsym->ctype.modifiers = mod;
		sym = newsym;
	}
	return sym;
}

static struct symbol *create_pointer(struct expression *expr, struct symbol *sym, int degenerate)
{
	struct symbol *node = alloc_symbol(expr->pos, SYM_NODE);
	struct symbol *ptr = alloc_symbol(expr->pos, SYM_PTR);

	node->ctype.base_type = ptr;
	ptr->bit_size = bits_in_pointer;
	ptr->ctype.alignment = pointer_alignment;

	node->bit_size = bits_in_pointer;
	node->ctype.alignment = pointer_alignment;

	access_symbol(sym);
	sym->ctype.modifiers |= MOD_ADDRESSABLE;
	if (sym->ctype.modifiers & MOD_REGISTER) {
		warning(expr->pos, "taking address of 'register' variable '%s'", show_ident(sym->ident));
		sym->ctype.modifiers &= ~MOD_REGISTER;
	}
	if (sym->type == SYM_NODE) {
		ptr->ctype.as |= sym->ctype.as;
		ptr->ctype.modifiers |= sym->ctype.modifiers;
		sym = sym->ctype.base_type;
	}
	if (degenerate && sym->type == SYM_ARRAY) {
		ptr->ctype.as |= sym->ctype.as;
		ptr->ctype.modifiers |= sym->ctype.modifiers;
		sym = sym->ctype.base_type;
	}
	ptr->ctype.base_type = sym;

	return node;
}

/* Arrays degenerate into pointers on pointer arithmetic */
static struct symbol *degenerate(struct expression *expr)
{
	struct symbol *ctype, *base;

	if (!expr)
		return NULL;
	ctype = expr->ctype;
	if (!ctype)
		return NULL;
	base = ctype;
	if (ctype->type == SYM_NODE)
		base = ctype->ctype.base_type;
	/*
	 * Arrays degenerate into pointers to the entries, while
	 * functions degenerate into pointers to themselves.
	 * If array was part of non-lvalue compound, we create a copy
	 * of that compound first and then act as if we were dealing with
	 * the corresponding field in there.
	 */
	switch (base->type) {
	case SYM_ARRAY:
		if (expr->type == EXPR_SLICE) {
			struct symbol *a = alloc_symbol(expr->pos, SYM_NODE);
			struct expression *e0, *e1, *e2, *e3, *e4;

			a->ctype.base_type = expr->base->ctype;
			a->bit_size = expr->base->ctype->bit_size;
			a->array_size = expr->base->ctype->array_size;

			e0 = alloc_expression(expr->pos, EXPR_SYMBOL);
			e0->symbol = a;
			e0->ctype = &lazy_ptr_ctype;

			e1 = alloc_expression(expr->pos, EXPR_PREOP);
			e1->unop = e0;
			e1->op = '*';
			e1->ctype = expr->base->ctype;	/* XXX */

			e2 = alloc_expression(expr->pos, EXPR_ASSIGNMENT);
			e2->left = e1;
			e2->right = expr->base;
			e2->op = '=';
			e2->ctype = expr->base->ctype;

			if (expr->r_bitpos) {
				e3 = alloc_expression(expr->pos, EXPR_BINOP);
				e3->op = '+';
				e3->left = e0;
				e3->right = alloc_const_expression(expr->pos,
							expr->r_bitpos >> 3);
				e3->ctype = &lazy_ptr_ctype;
			} else {
				e3 = e0;
			}

			e4 = alloc_expression(expr->pos, EXPR_COMMA);
			e4->left = e2;
			e4->right = e3;
			e4->ctype = &lazy_ptr_ctype;

			expr->unop = e4;
			expr->type = EXPR_PREOP;
			expr->op = '*';
		}
	case SYM_FN:
		if (expr->op != '*' || expr->type != EXPR_PREOP) {
			warning(expr->pos, "strange non-value function or array");
			return &bad_ctype;
		}
		*expr = *expr->unop;
		ctype = create_pointer(expr, ctype, 1);
		expr->ctype = ctype;
	default:
		/* nothing */;
	}
	return ctype;
}

static struct symbol *evaluate_addressof(struct expression *expr)
{
	struct expression *op = expr->unop;
	struct symbol *ctype;

	if (op->op != '*' || op->type != EXPR_PREOP) {
		warning(expr->pos, "not addressable");
		return NULL;
	}
	ctype = op->ctype;
	*expr = *op->unop;

	/*
	 * symbol expression evaluation is lazy about the type
	 * of the sub-expression, so we may have to generate
	 * the type here if so..
	 */
	if (expr->ctype == &lazy_ptr_ctype) {
		ctype = create_pointer(expr, ctype, 0);
		expr->ctype = ctype;
	}
	return expr->ctype;
}


static struct symbol *evaluate_dereference(struct expression *expr)
{
	struct expression *op = expr->unop;
	struct symbol *ctype = op->ctype, *node, *target;

	/* Simplify: *&(expr) => (expr) */
	if (op->type == EXPR_PREOP && op->op == '&') {
		*expr = *op->unop;
		return expr->ctype;
	}

	/* Dereferencing a node drops all the node information. */
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;

	node = alloc_symbol(expr->pos, SYM_NODE);
	target = ctype->ctype.base_type;

	switch (ctype->type) {
	default:
		warning(expr->pos, "cannot derefence this type");
		return NULL;
	case SYM_PTR:
		merge_type(node, ctype);
		if (ctype->type != SYM_ARRAY)
			break;
		/*
		 * Dereferencing a pointer to an array results in a
		 * degenerate dereference: the expression becomes
		 * just a pointer to the entry, and the derefence
		 * goes away.
		 */
		*expr = *op;

		target = alloc_symbol(expr->pos, SYM_PTR);
		target->bit_size = bits_in_pointer;
		target->ctype.alignment = pointer_alignment;
		merge_type(target, ctype->ctype.base_type);
		break;

	case SYM_ARRAY:
		if (!lvalue_expression(op)) {
			warning(op->pos, "non-lvalue array??");
			return NULL;
		}

		/* Do the implied "addressof" on the array */
		*op = *op->unop;

		/*
		 * When an array is dereferenced, we need to pick
		 * up the attributes of the original node too..
		 */
		merge_type(node, op->ctype);
		merge_type(node, ctype);
		break;
	}

	node->bit_size = target->bit_size;
	node->array_size = target->array_size;

	expr->ctype = node;
	return node;
}

/*
 * Unary post-ops: x++ and x--
 */
static struct symbol *evaluate_postop(struct expression *expr)
{
	struct expression *op = expr->unop;
	struct symbol *ctype = op->ctype;

	if (!lvalue_expression(expr->unop)) {
		warning(expr->pos, "need lvalue expression for ++/--");
		return NULL;
	}
	if (is_restricted_type(ctype) && restricted_unop(expr->op, ctype)) {
		warning(expr->pos, "bad operation on restricted");
		return NULL;
	}

	evaluate_assign_to(op, ctype);

	expr->ctype = ctype;
	return ctype;
}

static struct symbol *evaluate_sign(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;
	if (is_int_type(ctype)) {
		struct symbol *rtype = rtype = integer_promotion(ctype);
		if (rtype->bit_size != ctype->bit_size)
			expr->unop = cast_to(expr->unop, rtype);
		ctype = rtype;
	} else if (is_float_type(ctype) && expr->op != '~') {
		/* no conversions needed */
	} else if (is_restricted_type(ctype) && !restricted_unop(expr->op, ctype)) {
		/* no conversions needed */
	} else {
		return bad_expr_type(expr);
	}
	if (expr->op == '+')
		*expr = *expr->unop;
	expr->ctype = ctype;
	return ctype;
}

static struct symbol *evaluate_preop(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;

	switch (expr->op) {
	case '(':
		*expr = *expr->unop;
		return ctype;

	case '+':
	case '-':
	case '~':
		return evaluate_sign(expr);

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
		if (is_safe_type(ctype))
			warning(expr->pos, "testing a 'safe expression'");
		if (is_float_type(ctype)) {
			struct expression *arg = expr->unop;
			expr->type = EXPR_BINOP;
			expr->op = SPECIAL_EQUAL;
			expr->left = arg;
			expr->right = alloc_expression(expr->pos, EXPR_FVALUE);
			expr->right->ctype = ctype;
			expr->right->fvalue = 0;
		}
		ctype = &bool_ctype;
		break;

	default:
		break;
	}
	expr->ctype = ctype;
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

	/*
	 * Create a new add-expression
	 *
	 * NOTE! Even if we just add zero, we need a new node
	 * for the member pointer, since it has a different
	 * type than the original pointer. We could make that
	 * be just a cast, but the fact is, a node is a node,
	 * so we might as well just do the "add zero" here.
	 */
	add = alloc_expression(expr->pos, EXPR_BINOP);
	add->op = '+';
	add->left = expr;
	add->right = alloc_expression(expr->pos, EXPR_VALUE);
	add->right->ctype = &int_ctype;
	add->right->value = offset;

	/*
	 * The ctype of the pointer will be lazily evaluated if
	 * we ever take the address of this member dereference..
	 */
	add->ctype = &lazy_ptr_ctype;
	return add;
}

/* structure/union dereference */
static struct symbol *evaluate_member_dereference(struct expression *expr)
{
	int offset;
	struct symbol *ctype, *member;
	struct expression *deref = expr->deref, *add;
	struct ident *ident = expr->member;
	unsigned int mod;
	int address_space;

	if (!evaluate_expression(deref))
		return NULL;
	if (!ident) {
		warning(expr->pos, "bad member name");
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
	if (!ctype || (ctype->type != SYM_STRUCT && ctype->type != SYM_UNION)) {
		warning(expr->pos, "expected structure or union");
		return NULL;
	}
	offset = 0;
	member = find_identifier(ident, ctype->symbol_list, &offset);
	if (!member) {
		const char *type = ctype->type == SYM_STRUCT ? "struct" : "union";
		const char *name = "<unnamed>";
		int namelen = 9;
		if (ctype->ident) {
			name = ctype->ident->name;
			namelen = ctype->ident->len;
		}
		warning(expr->pos, "no member '%s' in %s %.*s",
			show_ident(ident), type, namelen, name);
		return NULL;
	}

	/*
	 * The member needs to take on the address space and modifiers of
	 * the "parent" type.
	 */
	member = convert_to_as_mod(member, address_space, mod);
	ctype = member->ctype.base_type;

	if (!lvalue_expression(deref)) {
		if (deref->type != EXPR_SLICE) {
			expr->base = deref;
			expr->r_bitpos = 0;
		} else {
			expr->base = deref->base;
			expr->r_bitpos = deref->r_bitpos;
		}
		expr->r_bitpos += offset << 3;
		expr->type = EXPR_SLICE;
		expr->r_nrbits = member->bit_size;
		expr->r_bitpos += member->bit_offset;
		expr->ctype = member;
		return member;
	}

	deref = deref->unop;
	expr->deref = deref;

	add = evaluate_offset(deref, offset);
	if (ctype->type == SYM_BITFIELD) {
		expr->type = EXPR_BITFIELD;
		expr->bitpos = member->bit_offset;
		expr->nrbits = member->bit_size;
		expr->address = add;
	} else {
		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->unop = add;
	}

	expr->ctype = member;
	return member;
}

static int is_promoted(struct expression *expr)
{
	while (1) {
		switch (expr->type) {
		case EXPR_BINOP:
		case EXPR_SELECT:
		case EXPR_CONDITIONAL:
			return 1;
		case EXPR_COMMA:
			expr = expr->right;
			continue;
		case EXPR_PREOP:
			switch (expr->op) {
			case '(':
				expr = expr->unop;
				continue;
			case '+':
			case '-':
			case '~':
				return 1;
			default:
				return 0;
			}
		default:
			return 0;
		}
	}
}


static struct symbol *evaluate_cast(struct expression *);

static struct symbol *evaluate_type_information(struct expression *expr)
{
	struct symbol *sym = expr->cast_type;
	if (!sym) {
		sym = evaluate_expression(expr->cast_expression);
		if (!sym)
			return NULL;
		/*
		 * Expressions of restricted types will possibly get
		 * promoted - check that here
		 */
		if (is_restricted_type(sym)) {
			if (sym->bit_size < bits_in_int && is_promoted(expr))
				sym = &int_ctype;
		}
	}
	examine_symbol_type(sym);
	if (is_bitfield_type(sym)) {
		warning(expr->pos, "trying to examine bitfield type");
		return NULL;
	}
	return sym;
}

static struct symbol *evaluate_sizeof(struct expression *expr)
{
	struct symbol *type;
	int size;

	type = evaluate_type_information(expr);
	if (!type)
		return NULL;

	size = type->bit_size;
	if (size & 7)
		warning(expr->pos, "cannot size expression");
	expr->type = EXPR_VALUE;
	expr->value = size >> 3;
	expr->ctype = size_t_ctype;
	return size_t_ctype;
}

static struct symbol *evaluate_ptrsizeof(struct expression *expr)
{
	struct symbol *type;
	int size;

	type = evaluate_type_information(expr);
	if (!type)
		return NULL;

	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (!type)
		return NULL;
	switch (type->type) {
	case SYM_ARRAY:
		break;
	case SYM_PTR:
		type = type->ctype.base_type;
		if (type)
			break;
	default:
		warning(expr->pos, "expected pointer expression");
		return NULL;
	}
	size = type->bit_size;
	if (size & 7)
		size = 0;
	expr->type = EXPR_VALUE;
	expr->value = size >> 3;
	expr->ctype = size_t_ctype;
	return size_t_ctype;
}

static struct symbol *evaluate_alignof(struct expression *expr)
{
	struct symbol *type;

	type = evaluate_type_information(expr);
	if (!type)
		return NULL;

	expr->type = EXPR_VALUE;
	expr->value = type->ctype.alignment;
	expr->ctype = size_t_ctype;
	return size_t_ctype;
}

static int evaluate_arguments(struct symbol *f, struct symbol *fn, struct expression_list *head)
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

		ctype = degenerate(expr);

		target = argtype;
		if (!target && ctype->bit_size < bits_in_int)
			target = &int_ctype;
		if (target) {
			static char where[30];
			examine_symbol_type(target);
			sprintf(where, "argument %d", i);
			compatible_assignment_types(expr, target, p, ctype, where);
		}

		i++;
		NEXT_PTR_LIST(argtype);
	} END_FOR_EACH_PTR(expr);
	FINISH_PTR_LIST(argtype);
	return 1;
}

static void evaluate_initializer(struct symbol *ctype, struct expression **ep);

static int evaluate_one_array_initializer(struct symbol *ctype, struct expression **ep, int current)
{
	struct expression *entry = *ep;
	struct expression **parent, *reuse = NULL;
	unsigned long offset;
	struct symbol *sym;
	unsigned long from, to;
	int accept_string = is_byte_type(ctype);

	from = current;
	to = from+1;
	parent = ep;
	if (entry->type == EXPR_INDEX) {
		from = entry->idx_from;
		to = entry->idx_to+1;
		parent = &entry->idx_expression;
		reuse = entry;
		entry = entry->idx_expression;
	}

	offset = from * (ctype->bit_size>>3);
	if (offset) {
		if (!reuse) reuse = alloc_expression(entry->pos, EXPR_POS);
		reuse->type = EXPR_POS;
		reuse->ctype = ctype;
		reuse->init_offset = offset;
		reuse->init_nr = to - from;
		reuse->init_expr = entry;
		parent = &reuse->init_expr;
		entry = reuse;
	}
	*ep = entry;

	if (accept_string && entry->type == EXPR_STRING) {
		sym = evaluate_expression(entry);
		to = from + get_expression_value(sym->array_size);
	} else {
		evaluate_initializer(ctype, parent);
	}
	return to;
}

static void evaluate_array_initializer(struct symbol *ctype, struct expression *expr)
{
	struct expression *entry;
	int current = 0;

	FOR_EACH_PTR(expr->expr_list, entry) {
		current = evaluate_one_array_initializer(ctype, THIS_ADDRESS(entry), current);
	} END_FOR_EACH_PTR(entry);
}

/* A scalar initializer is allowed, and acts pretty much like an array of one */
static void evaluate_scalar_initializer(struct symbol *ctype, struct expression *expr)
{
	if (expression_list_size(expr->expr_list) != 1) {
		warning(expr->pos, "unexpected compound initializer");
		return;
	}
	evaluate_array_initializer(ctype, expr);
	return;
}

static struct symbol *find_struct_ident(struct symbol *ctype, struct ident *ident)
{
	struct symbol *sym;

	FOR_EACH_PTR(ctype->symbol_list, sym) {
		if (sym->ident == ident)
			return sym;
	} END_FOR_EACH_PTR(sym);
	return NULL;
}

static int evaluate_one_struct_initializer(struct symbol *ctype, struct expression **ep, struct symbol *sym)
{
	struct expression *entry = *ep;
	struct expression **parent;
	struct expression *reuse = NULL;
	unsigned long offset;

	if (!sym) {
		error(entry->pos, "unknown named initializer");
		return -1;
	}

	if (entry->type == EXPR_IDENTIFIER) {
		reuse = entry;
		entry = entry->ident_expression;
	}

	parent = ep;
	offset = sym->offset;
	if (offset) {
		if (!reuse)
			reuse = alloc_expression(entry->pos, EXPR_POS);
		reuse->type = EXPR_POS;
		reuse->ctype = sym;
		reuse->init_offset = offset;
		reuse->init_nr = 1;
		reuse->init_expr = entry;
		parent = &reuse->init_expr;
		entry = reuse;
	}
	*ep = entry;
	evaluate_initializer(sym, parent);
	return 0;
}

static void evaluate_struct_or_union_initializer(struct symbol *ctype, struct expression *expr, int multiple)
{
	struct expression *entry;
	struct symbol *sym;

	PREPARE_PTR_LIST(ctype->symbol_list, sym);
	FOR_EACH_PTR(expr->expr_list, entry) {
		if (entry->type == EXPR_IDENTIFIER) {
			struct ident *ident = entry->expr_ident;
			/* We special-case the "already right place" case */
			if (!sym || sym->ident != ident) {
				RESET_PTR_LIST(sym);
				for (;;) {
					if (!sym)
						break;
					if (sym->ident == ident)
						break;
					NEXT_PTR_LIST(sym);
				}
			}
		}
		if (evaluate_one_struct_initializer(ctype, THIS_ADDRESS(entry), sym))
			return;
		NEXT_PTR_LIST(sym);
	} END_FOR_EACH_PTR(entry);
	FINISH_PTR_LIST(sym);
}

/*
 * Initializers are kind of like assignments. Except
 * they can be a hell of a lot more complex.
 */
static void evaluate_initializer(struct symbol *ctype, struct expression **ep)
{
	struct expression *expr = *ep;

	/*
	 * Simple non-structure/array initializers are the simple 
	 * case, and look (and parse) largely like assignments.
	 */
	switch (expr->type) {
	default: {
		int is_string = expr->type == EXPR_STRING;
		struct symbol *rtype = evaluate_expression(expr);
		if (rtype) {
			/*
			 * Special case:
			 * 	char array[] = "string"
			 * should _not_ degenerate.
			 */
			if (!is_string || !is_string_type(ctype))
				rtype = degenerate(expr);
			compatible_assignment_types(expr, ctype, ep, rtype, "initializer");
		}
		return;
	}

	case EXPR_INITIALIZER:
		expr->ctype = ctype;
		if (ctype->type == SYM_NODE)
			ctype = ctype->ctype.base_type;
	
		switch (ctype->type) {
		case SYM_ARRAY:
		case SYM_PTR:
			evaluate_array_initializer(ctype->ctype.base_type, expr);
			return;
		case SYM_UNION:
			evaluate_struct_or_union_initializer(ctype, expr, 0);
			return;
		case SYM_STRUCT:
			evaluate_struct_or_union_initializer(ctype, expr, 1);
			return;
		default:
			evaluate_scalar_initializer(ctype, expr);
			return;
		}

	case EXPR_IDENTIFIER:
		if (ctype->type == SYM_NODE)
			ctype = ctype->ctype.base_type;
		if (ctype->type != SYM_STRUCT && ctype->type != SYM_UNION) {
			error(expr->pos, "expected structure or union for '%s' dereference", show_ident(expr->expr_ident));
			show_symbol(ctype);
			return;
		}
		evaluate_one_struct_initializer(ctype, ep,
			find_struct_ident(ctype, expr->expr_ident));
		return;

	case EXPR_INDEX:
		if (ctype->type == SYM_NODE)
			ctype = ctype->ctype.base_type;
		if (ctype->type != SYM_ARRAY) {
			error(expr->pos, "expected array");
			return;
		}
		evaluate_one_array_initializer(ctype->ctype.base_type, ep, 0);
		return;

	case EXPR_POS:
		/*
		 * An EXPR_POS expression has already been evaluated, and we don't
		 * need to do anything more
		 */
		return;
	}
}

static int get_as(struct symbol *sym)
{
	int as;
	unsigned long mod;

	if (!sym)
		return 0;
	as = sym->ctype.as;
	mod = sym->ctype.modifiers;
	if (sym->type == SYM_NODE) {
		sym = sym->ctype.base_type;
		as |= sym->ctype.as;
		mod |= sym->ctype.modifiers;
	}

	/*
	 * At least for now, allow casting to a "unsigned long".
	 * That's how we do things like pointer arithmetic and
	 * store pointers to registers.
	 */
	if (sym == &ulong_ctype)
		return -1;

	if (sym && sym->type == SYM_PTR) {
		sym = sym->ctype.base_type;
		as |= sym->ctype.as;
		mod |= sym->ctype.modifiers;
	}
	if (mod & MOD_FORCE)
		return -1;
	return as;
}

static struct symbol *evaluate_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;
	struct symbol *ctype = examine_symbol_type(expr->cast_type);
	enum type type;

	if (!target)
		return NULL;

	expr->ctype = ctype;
	expr->cast_type = ctype;

	/*
	 * Special case: a cast can be followed by an
	 * initializer, in which case we need to pass
	 * the type value down to that initializer rather
	 * than trying to evaluate it as an expression
	 *
	 * A more complex case is when the initializer is
	 * dereferenced as part of a post-fix expression.
	 * We need to produce an expression that can be dereferenced.
	 */
	if (target->type == EXPR_INITIALIZER) {
		struct symbol *sym = expr->cast_type;
		struct expression *addr = alloc_expression(expr->pos, EXPR_SYMBOL);

		sym->initializer = expr->cast_expression;
		evaluate_symbol(sym);

		addr->ctype = &lazy_ptr_ctype;	/* Lazy eval */
		addr->symbol = sym;

		expr->type = EXPR_PREOP;
		expr->op = '*';
		expr->unop = addr;
		expr->ctype = sym;

		return sym;
	}

	evaluate_expression(target);
	degenerate(target);

	/*
	 * You can always throw a value away by casting to
	 * "void" - that's an implicit "force". Note that
	 * the same is _not_ true of "void *".
	 */
	if (ctype == &void_ctype)
		goto out;

	type = ctype->type;
	if (type == SYM_NODE) {
		type = ctype->ctype.base_type->type;
		if (ctype->ctype.base_type == &void_ctype)
			goto out;
	}
	if (type == SYM_ARRAY || type == SYM_UNION || type == SYM_STRUCT)
		warning(expr->pos, "cast to non-scalar");

	if (!target->ctype) {
		warning(expr->pos, "cast from unknown type");
		goto out;
	}

	type = target->ctype->type;
	if (type == SYM_NODE)
		type = target->ctype->ctype.base_type->type;
	if (type == SYM_ARRAY || type == SYM_UNION || type == SYM_STRUCT)
		warning(expr->pos, "cast from non-scalar");

	if (!get_as(ctype) && get_as(target->ctype) > 0)
		warning(expr->pos, "cast removes address space of expression");

	if (!(ctype->ctype.modifiers & MOD_FORCE)) {
		struct symbol *t1 = ctype, *t2 = target->ctype;
		if (t1->type == SYM_NODE)
			t1 = t1->ctype.base_type;
		if (t2->type == SYM_NODE)
			t2 = t2->ctype.base_type;
		if (t1 != t2) {
			if (t1->type == SYM_RESTRICT)
				warning(expr->pos, "cast to restricted type");
			if (t2->type == SYM_RESTRICT)
				warning(expr->pos, "cast from restricted type");
		}
	}

	/*
	 * Casts of constant values are special: they
	 * can be NULL, and thus need to be simplified
	 * early.
	 */
	if (target->type == EXPR_VALUE)
		cast_value(expr, ctype, target, target->ctype);

out:
	return ctype;
}

/*
 * Evaluate a call expression with a symbol. This
 * should expand inline functions, and evaluate
 * builtins.
 */
static int evaluate_symbol_call(struct expression *expr)
{
	struct expression *fn = expr->fn;
	struct symbol *ctype = fn->ctype;

	if (fn->type != EXPR_PREOP)
		return 0;

	if (ctype->op && ctype->op->evaluate)
		return ctype->op->evaluate(expr);

	if (ctype->ctype.modifiers & MOD_INLINE) {
		int ret;
		struct symbol *curr = current_fn;
		current_fn = ctype->ctype.base_type;
		examine_fn_arguments(current_fn);

		ret = inline_function(expr, ctype);

		/* restore the old function */
		current_fn = curr;
		return ret;
	}

	return 0;
}

static struct symbol *evaluate_call(struct expression *expr)
{
	int args, fnargs;
	struct symbol *ctype, *sym;
	struct expression *fn = expr->fn;
	struct expression_list *arglist = expr->args;

	if (!evaluate_expression(fn))
		return NULL;
	sym = ctype = fn->ctype;
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type == SYM_PTR || ctype->type == SYM_ARRAY)
		ctype = ctype->ctype.base_type;
	if (!evaluate_arguments(sym, ctype, arglist))
		return NULL;
	if (ctype->type != SYM_FN) {
		warning(expr->pos, "not a function %s", show_ident(sym->ident));
		return NULL;
	}
	args = expression_list_size(expr->args);
	fnargs = symbol_list_size(ctype->arguments);
	if (args < fnargs)
		warning(expr->pos, "not enough arguments for function %s", show_ident(sym->ident));
	if (args > fnargs && !ctype->variadic)
		warning(expr->pos, "too many arguments for function %s", show_ident(sym->ident));
	if (sym->type == SYM_NODE) {
		if (evaluate_symbol_call(expr))
			return expr->ctype;
	}
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
	case EXPR_FVALUE:
		warning(expr->pos, "value expression without a type");
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
		evaluate_expression(expr->left);
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
	case EXPR_PTRSIZEOF:
		return evaluate_ptrsizeof(expr);
	case EXPR_ALIGNOF:
		return evaluate_alignof(expr);
	case EXPR_DEREF:
		return evaluate_member_dereference(expr);
	case EXPR_CALL:
		return evaluate_call(expr);
	case EXPR_BITFIELD:
		warning(expr->pos, "bitfield generated by parser");
		return NULL;
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		if (expr->cond_true)
			return evaluate_conditional_expression(expr);
		else
			return evaluate_short_conditional(expr);
	case EXPR_STATEMENT:
		expr->ctype = evaluate_statement(expr->statement);
		return expr->ctype;

	case EXPR_LABEL:
		expr->ctype = &ptr_ctype;
		return &ptr_ctype;

	case EXPR_TYPE:
		/* Evaluate the type of the symbol .. */
		evaluate_symbol(expr->symbol);
		/* .. but the type of the _expression_ is a "type" */
		expr->ctype = &type_ctype;
		return &type_ctype;

	/* These can not exist as stand-alone expressions */
	case EXPR_INITIALIZER:
	case EXPR_IDENTIFIER:
	case EXPR_INDEX:
	case EXPR_POS:
		warning(expr->pos, "internal front-end error: initializer in expression");
		return NULL;
	case EXPR_SLICE:
		warning(expr->pos, "internal front-end error: SLICE re-evaluated");
		return NULL;
	}
	return NULL;
}

void check_duplicates(struct symbol *sym)
{
	struct symbol *next = sym;

	while ((next = next->same_symbol) != NULL) {
		const char *typediff;
		evaluate_symbol(next);
		typediff = type_difference(sym, next, 0, 0);
		if (typediff) {
			warning(sym->pos, "symbol '%s' redeclared with different type (originally declared at %s:%d) - %s",
				show_ident(sym->ident),
				input_streams[next->pos.stream].name, next->pos.line, typediff);
			return;
		}
	}
}

struct symbol *evaluate_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return sym;

	sym = examine_symbol_type(sym);
	base_type = sym->ctype.base_type;
	if (!base_type)
		return NULL;

	/* Evaluate the initializers */
	if (sym->initializer)
		evaluate_initializer(sym, &sym->initializer);

	/* And finally, evaluate the body of the symbol too */
	if (base_type->type == SYM_FN) {
		struct symbol *curr = current_fn;

		current_fn = base_type;

		examine_fn_arguments(base_type);
		if (!base_type->stmt && base_type->inline_stmt)
			uninline(sym);
		if (base_type->stmt)
			evaluate_statement(base_type->stmt);

		current_fn = curr;
	}

	return base_type;
}

struct symbol *evaluate_return_expression(struct statement *stmt)
{
	struct expression *expr = stmt->expression;
	struct symbol *ctype, *fntype;

	evaluate_expression(expr);
	ctype = degenerate(expr);
	fntype = current_fn->ctype.base_type;
	if (!fntype || fntype == &void_ctype) {
		if (expr && ctype != &void_ctype)
			warning(expr->pos, "return expression in %s function", fntype?"void":"typeless");
		return NULL;
	}

	if (!expr) {
		warning(stmt->pos, "return with no return value");
		return NULL;
	}
	if (!ctype)
		return NULL;
	compatible_assignment_types(expr, fntype, &stmt->expression, ctype, "return expression");
	return NULL;
}

static void evaluate_if_statement(struct statement *stmt)
{
	if (!stmt->if_conditional)
		return;

	evaluate_conditional(stmt->if_conditional);
	evaluate_statement(stmt->if_true);
	evaluate_statement(stmt->if_false);
}

static void evaluate_iterator(struct statement *stmt)
{
	evaluate_conditional(stmt->iterator_pre_condition);
	evaluate_conditional(stmt->iterator_post_condition);
	evaluate_statement(stmt->iterator_pre_statement);
	evaluate_statement(stmt->iterator_statement);
	evaluate_statement(stmt->iterator_post_statement);
}

struct symbol *evaluate_statement(struct statement *stmt)
{
	if (!stmt)
		return NULL;

	switch (stmt->type) {
	case STMT_RETURN:
		return evaluate_return_expression(stmt);

	case STMT_EXPRESSION:
		if (!evaluate_expression(stmt->expression))
			return NULL;
		return degenerate(stmt->expression);

	case STMT_COMPOUND: {
		struct statement *s;
		struct symbol *type = NULL;
		struct symbol *sym;

		/* Evaluate each symbol in the compound statement */
		FOR_EACH_PTR(stmt->syms, sym) {
			evaluate_symbol(sym);
		} END_FOR_EACH_PTR(sym);
		evaluate_symbol(stmt->ret);

		/*
		 * Then, evaluate each statement, making the type of the
		 * compound statement be the type of the last statement
		 */
		type = NULL;
		FOR_EACH_PTR(stmt->stmts, s) {
			type = evaluate_statement(s);
		} END_FOR_EACH_PTR(s);
		if (!type)
			type = &void_ctype;
		return type;
	}
	case STMT_IF:
		evaluate_if_statement(stmt);
		return NULL;
	case STMT_ITERATOR:
		evaluate_iterator(stmt);
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
		return evaluate_statement(stmt->label_statement);
	case STMT_GOTO:
		evaluate_expression(stmt->goto_expression);
		return NULL;
	case STMT_NONE:
		break;
	case STMT_ASM:
		/* FIXME! Do the asm parameter evaluation! */
		break;
	case STMT_INTERNAL:
		evaluate_expression(stmt->expression);
		return NULL;
	}
	return NULL;
}
