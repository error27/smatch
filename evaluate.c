/*
 * sparse/evaluate.c
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
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
#include "allocate.h"
#include "parse.h"
#include "token.h"
#include "symbol.h"
#include "target.h"
#include "expression.h"

struct symbol *current_fn;

static struct symbol *degenerate(struct expression *expr);
static struct symbol *evaluate_symbol(struct symbol *sym);

static struct symbol *evaluate_symbol_expression(struct expression *expr)
{
	struct expression *addr;
	struct symbol *sym = expr->symbol;
	struct symbol *base_type;

	if (!sym) {
		error(expr->pos, "undefined identifier '%s'", show_ident(expr->symbol_name));
		return NULL;
	}

	examine_symbol_type(sym);

	base_type = get_base_type(sym);
	if (!base_type) {
		error(expr->pos, "identifier '%s' has no type", show_ident(expr->symbol_name));
		return NULL;
	}

	addr = alloc_expression(expr->pos, EXPR_SYMBOL);
	addr->symbol = sym;
	addr->symbol_name = expr->symbol_name;
	addr->ctype = &lazy_ptr_ctype;	/* Lazy evaluation: we need to do a proper job if somebody does &sym */
	expr->type = EXPR_PREOP;
	expr->op = '*';
	expr->unop = addr;

	/* The type of a symbol is the symbol itself! */
	expr->ctype = sym;
	return sym;
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

static int same_cast_type(struct symbol *orig, struct symbol *new)
{
	return orig->bit_size == new->bit_size && orig->bit_offset == orig->bit_offset;
}

static struct symbol *base_type(struct symbol *node, unsigned long *modp, unsigned long *asp)
{
	unsigned long mod, as;

	mod = 0; as = 0;
	while (node) {
		mod |= node->ctype.modifiers;
		as |= node->ctype.as;
		if (node->type == SYM_NODE) {
			node = node->ctype.base_type;
			continue;
		}
		break;
	}
	*modp = mod & ~MOD_IGNORE;
	*asp = as;
	return node;
}

static int is_same_type(struct expression *expr, struct symbol *new)
{
	struct symbol *old = expr->ctype;
	unsigned long oldmod, newmod, oldas, newas;

	old = base_type(old, &oldmod, &oldas);
	new = base_type(new, &newmod, &newas);

	/* Same base type, same address space? */
	if (old == new && oldas == newas) {
		unsigned long difmod;

		/* Check the modifier bits. */
		difmod = (oldmod ^ newmod) & ~MOD_NOCAST;

		/* Exact same type? */
		if (!difmod)
			return 1;

		/*
		 * Not the same type, but differs only in "const".
		 * Don't warn about MOD_NOCAST.
		 */
		if (difmod == MOD_CONST)
			return 0;
	}
	if ((oldmod | newmod) & MOD_NOCAST) {
		const char *tofrom = "to/from";
		if (!(newmod & MOD_NOCAST))
			tofrom = "from";
		if (!(oldmod & MOD_NOCAST))
			tofrom = "to";
		warning(expr->pos, "implicit cast %s nocast type", tofrom);
	}
	return 0;
}

/*
 * This gets called for implicit casts in assignments and
 * integer promotion. We often want to try to move the
 * cast down, because the ops involved may have been
 * implicitly cast up, and we can get rid of the casts
 * early.
 */
static struct expression * cast_to(struct expression *old, struct symbol *type)
{
	struct expression *expr;

	if (is_same_type(old, type))
		return old;

	/*
	 * See if we can simplify the op. Move the cast down.
	 */
	switch (old->type) {
	case EXPR_PREOP:
		if (old->op == '~') {
			old->ctype = type;
			old->unop = cast_to(old->unop, type);
			return old;
		}
		break;

	case EXPR_IMPLIED_CAST:
		if (old->ctype->bit_size >= type->bit_size) {
			struct expression *orig = old->cast_expression;
			if (same_cast_type(orig->ctype, type))
				return orig;
			if (old->ctype->bit_offset == type->bit_offset) {
				old->ctype = type;
				old->cast_type = type;
				return old;
			}
		}
		break;

	default:
		/* nothing */;
	}

	expr = alloc_expression(old->pos, EXPR_IMPLIED_CAST);
	expr->ctype = type;
	expr->cast_type = type;
	expr->cast_expression = old;
	return expr;
}

static int is_type_type(struct symbol *type)
{
	return (type->ctype.modifiers & MOD_TYPE) != 0;
}

int is_ptr_type(struct symbol *type)
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
	error(expr->pos, "incompatible types for operation (%s)", show_special(expr->op));
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

		*lp = cast_to(left, ctype);
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
		case '=':
		case SPECIAL_EQUAL:
		case SPECIAL_NOTEQUAL:
		case SPECIAL_AND_ASSIGN:
		case SPECIAL_OR_ASSIGN:
		case SPECIAL_XOR_ASSIGN:
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
	return expr->type == EXPR_PREOP && expr->op == '*';
}

static int ptr_object_size(struct symbol *ptr_type)
{
	if (ptr_type->type == SYM_NODE)
		ptr_type = ptr_type->ctype.base_type;
	if (ptr_type->type == SYM_PTR)
		ptr_type = get_base_type(ptr_type);
	return ptr_type->bit_size;
}

static struct symbol *evaluate_ptr_add(struct expression *expr, struct symbol *ctype, struct expression **ip)
{
	struct expression *i = *ip;
	struct symbol *ptr_type = ctype;
	int bit_size;

	if (ptr_type->type == SYM_NODE)
		ptr_type = ptr_type->ctype.base_type;

	if (!is_int_type(i->ctype))
		return bad_expr_type(expr);

	examine_symbol_type(ctype);

	if (!ctype->ctype.base_type) {
		error(expr->pos, "missing type information");
		return NULL;
	}

	/* Get the size of whatever the pointer points to */
	bit_size = ptr_object_size(ctype);

	if (bit_size > bits_in_char) {
		int multiply = bit_size >> 3;
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);

		if (i->type == EXPR_VALUE) {
			val->value = i->value * multiply;
			val->ctype = size_t_ctype;
			*ip = val;
		} else {
			struct expression *mul = alloc_expression(expr->pos, EXPR_BINOP);

			val->ctype = size_t_ctype;
			val->value = bit_size >> 3;

			mul->op = '*';
			mul->ctype = size_t_ctype;
			mul->left = i;
			mul->right = val;

			*ip = mul;
		}
	}

	expr->ctype = ctype;	
	return ctype;
}

static struct symbol *evaluate_add(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct symbol *ltype = left->ctype, *rtype = right->ctype;

	if (is_ptr_type(ltype))
		return evaluate_ptr_add(expr, degenerate(left), &expr->right);

	if (is_ptr_type(rtype))
		return evaluate_ptr_add(expr, degenerate(right), &expr->left);
		
	return evaluate_arith(expr, 1);
}

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
			base1 = examine_symbol_type(base1);
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
			base2 = examine_symbol_type(base2);
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

static struct symbol *evaluate_ptr_sub(struct expression *expr, struct expression *l, struct expression **rp)
{
	const char *typediff;
	struct symbol *ctype;
	struct symbol *ltype, *rtype;
	struct expression *r = *rp;

	ltype = degenerate(l);
	rtype = degenerate(r);

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, degenerate(l), rp);

	ctype = ltype;
	typediff = type_difference(ltype, rtype, ~MOD_SIZE, ~MOD_SIZE);
	if (typediff) {
		ctype = common_ptr_type(l, r);
		if (!ctype) {
			error(expr->pos, "subtraction of different types can't work (%s)", typediff);
			return NULL;
		}
	}
	examine_symbol_type(ctype);

	/* Figure out the base type we point to */
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
		error(expr->pos, "subtraction of functions? Share your drugs");
		return NULL;
	}
	ctype = get_base_type(ctype);

	expr->ctype = ssize_t_ctype;
	if (ctype->bit_size > bits_in_char) {
		struct expression *sub = alloc_expression(expr->pos, EXPR_BINOP);
		struct expression *div = expr;
		struct expression *val = alloc_expression(expr->pos, EXPR_VALUE);
		unsigned long value = ctype->bit_size >> 3;

		val->ctype = size_t_ctype;
		val->value = value;

		if (value & (value-1)) {
			if (Wptr_subtraction_blows)
				warning(expr->pos, "potentially expensive pointer subtraction");
		}

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
	struct expression *left = expr->left;
	struct symbol *ltype = left->ctype;

	if (is_ptr_type(ltype))
		return evaluate_ptr_sub(expr, left, &expr->right);

	return evaluate_arith(expr, 1);
}

#define is_safe_type(type) ((type)->ctype.modifiers & MOD_SAFE)

static struct symbol *evaluate_conditional(struct expression *expr, int iterator)
{
	struct symbol *ctype;

	if (!expr)
		return NULL;

	if (!iterator && expr->type == EXPR_ASSIGNMENT && expr->op == '=')
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
	if (!evaluate_conditional(expr->left, 0))
		return NULL;
	if (!evaluate_conditional(expr->right, 0))
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
		expr->left = cast_to(expr->left, ctype);
		expr->ctype = ctype;
		ctype = integer_promotion(rtype);
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
		expr->op = modify_for_unsigned(expr->op);
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
	if (ctype) {
		if (ctype->ctype.modifiers & MOD_UNSIGNED)
			expr->op = modify_for_unsigned(expr->op);
		goto OK;
	}

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

/*
 * NOTE! The degenerate case of "x ? : y", where we don't
 * have a true case, this will possibly promote "x" to the
 * same type as "y", and thus _change_ the conditional
 * test in the expression. But since promotion is "safe"
 * for testing, that's ok.
 */
static struct symbol *evaluate_conditional_expression(struct expression *expr)
{
	struct expression **true;
	struct symbol *ctype, *ltype, *rtype;
	const char * typediff;

	if (!evaluate_conditional(expr->conditional, 0))
		return NULL;
	if (!evaluate_expression(expr->cond_false))
		return NULL;

	ctype = degenerate(expr->conditional);
	rtype = degenerate(expr->cond_false);

	true = &expr->conditional;
	ltype = ctype;
	if (expr->cond_true) {
		if (!evaluate_expression(expr->cond_true))
			return NULL;
		ltype = degenerate(expr->cond_true);
		true = &expr->cond_true;
	}

	ctype = compatible_integer_binop(true, &expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_ptr_type(*true, expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_float_binop(true, &expr->cond_false);
	if (ctype)
		goto out;
	ctype = compatible_restricted_binop('?', true, &expr->cond_false);
	if (ctype)
		goto out;
	ctype = ltype;
	typediff = type_difference(ltype, rtype, MOD_IGN, MOD_IGN);
	if (!typediff)
		goto out;
	error(expr->pos, "incompatible types in conditional expression (%s)", typediff);
	return NULL;

out:
	expr->ctype = ctype;
	return ctype;
}

/* FP assignments can not do modulo or bit operations */
static int compatible_float_op(int op)
{
	return	op == '=' ||
		op == SPECIAL_ADD_ASSIGN ||
		op == SPECIAL_SUB_ASSIGN ||
		op == SPECIAL_MUL_ASSIGN ||
		op == SPECIAL_DIV_ASSIGN;
}

static int compatible_assignment_types(struct expression *expr, struct symbol *target,
	struct expression **rp, struct symbol *source, const char *where, int op)
{
	const char *typediff;
	struct symbol *t;
	int target_as;

	if (is_int_type(target)) {
		if (is_int_type(source))
			goto Cast;
		if (is_float_type(source))
			goto Cast;
	} else if (is_float_type(target)) {
		if (!compatible_float_op(op)) {
			error(expr->pos, "invalid assignment");
			return 0;
		}
		if (is_int_type(source))
			goto Cast;
		if (is_float_type(source))
			goto Cast;
	} else if (is_restricted_type(target)) {
		if (restricted_binop(op, target)) {
			error(expr->pos, "bad restricted assignment");
			return 0;
		}
		if (!restricted_value(*rp, target))
			return 1;
	} else if (is_ptr_type(target)) {
		if (op == SPECIAL_ADD_ASSIGN || op == SPECIAL_SUB_ASSIGN) {
			evaluate_ptr_add(expr, target, rp);
			return 1;
		}
		if (op != '=') {
			error(expr->pos, "invalid pointer assignment");
			return 0;
		}
	} else if (op != '=') {
		error(expr->pos, "invalid assignment");
		return 0;
	}

	/* It's ok if the target is more volatile or const than the source */
	typediff = type_difference(target, source, MOD_VOLATILE | MOD_CONST, 0);
	if (!typediff)
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
			goto Cast;

		/* "void *" matches anything as long as the address space is ok */
		source_as = s->ctype.as;
		if (s->type == SYM_NODE) {
			s = s->ctype.base_type;
			source_as |= s->ctype.as;
		}
		if (source_as == target_as && (s->type == SYM_PTR || s->type == SYM_ARRAY)) {
			s = get_base_type(s);
			t = get_base_type(t);
			if (s == &void_ctype || t == &void_ctype)
				goto Cast;
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

static void mark_assigned(struct expression *expr)
{
	struct symbol *sym;

	if (!expr)
		return;
	switch (expr->type) {
	case EXPR_SYMBOL:
		sym = expr->symbol;
		if (!sym)
			return;
		if (sym->type != SYM_NODE)
			return;
		sym->ctype.modifiers |= MOD_ASSIGNED;
		return;

	case EXPR_BINOP:
		mark_assigned(expr->left);
		mark_assigned(expr->right);
		return;
	case EXPR_CAST:
		mark_assigned(expr->cast_expression);
		return;
	case EXPR_SLICE:
		mark_assigned(expr->base);
		return;
	default:
		/* Hmm? */
		return;
	}
}

static void evaluate_assign_to(struct expression *left, struct symbol *type)
{
	if (type->ctype.modifiers & MOD_CONST)
		error(left->pos, "assignment to const expression");

	/* We know left is an lvalue, so it's a "preop-*" */
	mark_assigned(left->unop);
}

static struct symbol *evaluate_assignment(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct expression *where = expr;
	struct symbol *ltype, *rtype;

	if (!lvalue_expression(left)) {
		error(expr->pos, "not an lvalue");
		return NULL;
	}

	ltype = left->ctype;

	rtype = degenerate(right);

	if (!compatible_assignment_types(where, ltype, &where->right, rtype, "assignment", expr->op))
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
				s->examined = 0;
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
	/* Take the modifiers of the pointer, and apply them to the member */
	mod |= sym->ctype.modifiers;
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
	base = examine_symbol_type(ctype);
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
			error(expr->pos, "strange non-value function or array");
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
		error(expr->pos, "not addressable");
		return NULL;
	}
	ctype = op->ctype;
	*expr = *op->unop;

	if (expr->type == EXPR_SYMBOL) {
		struct symbol *sym = expr->symbol;
		sym->ctype.modifiers |= MOD_ADDRESSABLE;
	}

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
		error(expr->pos, "cannot derefence this type");
		return NULL;
	case SYM_PTR:
		node->ctype.modifiers = target->ctype.modifiers & MOD_SPECIFIER;
		merge_type(node, ctype);
		break;

	case SYM_ARRAY:
		if (!lvalue_expression(op)) {
			error(op->pos, "non-lvalue array??");
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
		error(expr->pos, "need lvalue expression for ++/--");
		return NULL;
	}
	if (is_restricted_type(ctype) && restricted_unop(expr->op, ctype)) {
		error(expr->pos, "bad operation on restricted");
		return NULL;
	}

	evaluate_assign_to(op, ctype);

	expr->ctype = ctype;
	expr->op_value = 1;
	if (is_ptr_type(ctype))
		expr->op_value = ptr_object_size(ctype) >> 3;

	return ctype;
}

static struct symbol *evaluate_sign(struct expression *expr)
{
	struct symbol *ctype = expr->unop->ctype;
	if (is_int_type(ctype)) {
		struct symbol *rtype = rtype = integer_promotion(ctype);
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

static struct symbol *find_identifier(struct ident *ident, struct symbol_list *_list, int *offset)
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
		error(expr->pos, "bad member name");
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
		error(expr->pos, "expected structure or union");
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
		error(expr->pos, "no member '%s' in %s %.*s",
			show_ident(ident), type, namelen, name);
		return NULL;
	}

	/*
	 * The member needs to take on the address space and modifiers of
	 * the "parent" type.
	 */
	member = convert_to_as_mod(member, address_space, mod);
	ctype = get_base_type(member);

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
	expr->type = EXPR_PREOP;
	expr->op = '*';
	expr->unop = add;

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
		error(expr->pos, "trying to examine bitfield type");
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
	if ((size < 0) || (size & 7))
		error(expr->pos, "cannot size expression");
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
		type = get_base_type(type);
		if (type)
			break;
	default:
		error(expr->pos, "expected pointer expression");
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
			compatible_assignment_types(expr, target, p, ctype, where, '=');
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
		error(expr->pos, "unexpected compound initializer");
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
			compatible_assignment_types(expr, ctype, ep, rtype, "initializer", '=');
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
			evaluate_array_initializer(get_base_type(ctype), expr);
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
		sym = get_base_type(sym);
		as |= sym->ctype.as;
		mod |= sym->ctype.modifiers;
	}
	if (mod & MOD_FORCE)
		return -1;
	return as;
}

static void cast_to_as(struct expression *e, int as)
{
	struct expression *v = e->cast_expression;

	if (!Wcast_to_address_space)
		return;

	/* cast from constant 0 to pointer is OK */
	if (v->type == EXPR_VALUE && is_int_type(v->ctype) && !v->value)
		return;

	warning(e->pos, "cast adds address space to expression (<asn:%d>)", as);
}

static struct symbol *evaluate_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;
	struct symbol *ctype = examine_symbol_type(expr->cast_type);
	struct symbol *t1, *t2;
	enum type type1, type2;
	int as1, as2;

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

	t1 = ctype;
	if (t1->type == SYM_NODE)
		t1 = t1->ctype.base_type;
	if (t1->type == SYM_ENUM)
		t1 = t1->ctype.base_type;

	/*
	 * You can always throw a value away by casting to
	 * "void" - that's an implicit "force". Note that
	 * the same is _not_ true of "void *".
	 */
	if (t1 == &void_ctype)
		goto out;

	type1 = t1->type;
	if (type1 == SYM_ARRAY || type1 == SYM_UNION || type1 == SYM_STRUCT)
		warning(expr->pos, "cast to non-scalar");

	t2 = target->ctype;
	if (!t2) {
		error(expr->pos, "cast from unknown type");
		goto out;
	}
	if (t2->type == SYM_NODE)
		t2 = t2->ctype.base_type;
	if (t2->type == SYM_ENUM)
		t2 = t2->ctype.base_type;

	type2 = t2->type;
	if (type2 == SYM_ARRAY || type2 == SYM_UNION || type2 == SYM_STRUCT)
		warning(expr->pos, "cast from non-scalar");

	if (!(ctype->ctype.modifiers & MOD_FORCE) && t1 != t2) {
		if (t1->type == SYM_RESTRICT)
			warning(expr->pos, "cast to restricted type");
		if (t2->type == SYM_RESTRICT)
			warning(expr->pos, "cast from restricted type");
	}

	as1 = get_as(ctype);
	as2 = get_as(target->ctype);
	if (!as1 && as2 > 0)
		warning(expr->pos, "cast removes address space of expression");
	if (as1 > 0 && as2 > 0 && as1 != as2)
		warning(expr->pos, "cast between address spaces (<asn:%d>-><asn:%d>)", as2, as1);
	if (as1 > 0 && !as2)
		cast_to_as(expr, as1);

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
		ctype = get_base_type(ctype);
	if (!evaluate_arguments(sym, ctype, arglist))
		return NULL;
	if (ctype->type != SYM_FN) {
		error(expr->pos, "not a function %s", show_ident(sym->ident));
		return NULL;
	}
	args = expression_list_size(expr->args);
	fnargs = symbol_list_size(ctype->arguments);
	if (args < fnargs)
		error(expr->pos, "not enough arguments for function %s", show_ident(sym->ident));
	if (args > fnargs && !ctype->variadic)
		error(expr->pos, "too many arguments for function %s", show_ident(sym->ident));
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
		error(expr->pos, "value expression without a type");
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
	case EXPR_IMPLIED_CAST:
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
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		return evaluate_conditional_expression(expr);
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
		error(expr->pos, "internal front-end error: initializer in expression");
		return NULL;
	case EXPR_SLICE:
		error(expr->pos, "internal front-end error: SLICE re-evaluated");
		return NULL;
	}
	return NULL;
}

static void check_duplicates(struct symbol *sym)
{
	int declared = 0;
	struct symbol *next = sym;

	while ((next = next->same_symbol) != NULL) {
		const char *typediff;
		evaluate_symbol(next);
		declared++;
		typediff = type_difference(sym, next, 0, 0);
		if (typediff) {
			error(sym->pos, "symbol '%s' redeclared with different type (originally declared at %s:%d) - %s",
				show_ident(sym->ident),
				stream_name(next->pos.stream), next->pos.line, typediff);
			return;
		}
	}
	if (!declared) {
		unsigned long mod = sym->ctype.modifiers;
		if (mod & (MOD_STATIC | MOD_REGISTER))
			return;
		if (!(mod & MOD_TOPLEVEL))
			return;
		if (sym->ident == &main_ident)
			return;
		warning(sym->pos, "symbol '%s' was not declared. Should it be static?", show_ident(sym->ident));
	}
}

static struct symbol *evaluate_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return sym;

	sym = examine_symbol_type(sym);
	base_type = get_base_type(sym);
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

void evaluate_symbol_list(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		check_duplicates(sym);
		evaluate_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

static struct symbol *evaluate_return_expression(struct statement *stmt)
{
	struct expression *expr = stmt->expression;
	struct symbol *ctype, *fntype;

	evaluate_expression(expr);
	ctype = degenerate(expr);
	fntype = current_fn->ctype.base_type;
	if (!fntype || fntype == &void_ctype) {
		if (expr && ctype != &void_ctype)
			error(expr->pos, "return expression in %s function", fntype?"void":"typeless");
		return NULL;
	}

	if (!expr) {
		error(stmt->pos, "return with no return value");
		return NULL;
	}
	if (!ctype)
		return NULL;
	compatible_assignment_types(expr, fntype, &stmt->expression, ctype, "return expression", '=');
	return NULL;
}

static void evaluate_if_statement(struct statement *stmt)
{
	if (!stmt->if_conditional)
		return;

	evaluate_conditional(stmt->if_conditional, 0);
	evaluate_statement(stmt->if_true);
	evaluate_statement(stmt->if_false);
}

static void evaluate_iterator(struct statement *stmt)
{
	evaluate_conditional(stmt->iterator_pre_condition, 1);
	evaluate_conditional(stmt->iterator_post_condition,1);
	evaluate_statement(stmt->iterator_pre_statement);
	evaluate_statement(stmt->iterator_statement);
	evaluate_statement(stmt->iterator_post_statement);
}

static void verify_output_constraint(struct expression *expr, const char *constraint)
{
	switch (*constraint) {
	case '=':	/* Assignment */
	case '+':	/* Update */
		break;
	default:
		error(expr->pos, "output constraint is not an assignment constraint (\"%s\")", constraint);
	}
}

static void verify_input_constraint(struct expression *expr, const char *constraint)
{
	switch (*constraint) {
	case '=':	/* Assignment */
	case '+':	/* Update */
		error(expr->pos, "input constraint with assignment (\"%s\")", constraint);
	}
}

static void evaluate_asm_statement(struct statement *stmt)
{
	struct expression *expr;
	int state;

	expr = stmt->asm_string;
	if (!expr || expr->type != EXPR_STRING) {
		error(stmt->pos, "need constant string for inline asm");
		return;
	}

	state = 0;
	FOR_EACH_PTR(stmt->asm_outputs, expr) {
		struct ident *ident;

		switch (state) {
		case 0: /* Identifier */
			state = 1;
			ident = (struct ident *)expr;
			continue;

		case 1: /* Constraint */
			state = 2;
			if (!expr || expr->type != EXPR_STRING) {
				error(expr ? expr->pos : stmt->pos, "asm output constraint is not a string");
				*THIS_ADDRESS(expr) = NULL;
				continue;
			}
			verify_output_constraint(expr, expr->string->data);
			continue;

		case 2: /* Expression */
			state = 0;
			if (!evaluate_expression(expr))
				return;
			if (!lvalue_expression(expr))
				warning(expr->pos, "asm output is not an lvalue");
			evaluate_assign_to(expr, expr->ctype);
			continue;
		}
	} END_FOR_EACH_PTR(expr);

	state = 0;
	FOR_EACH_PTR(stmt->asm_inputs, expr) {
		struct ident *ident;

		switch (state) {
		case 0: /* Identifier */
			state = 1;
			ident = (struct ident *)expr;
			continue;

		case 1:	/* Constraint */
			state = 2;
			if (!expr || expr->type != EXPR_STRING) {
				error(expr ? expr->pos : stmt->pos, "asm input constraint is not a string");
				*THIS_ADDRESS(expr) = NULL;
				continue;
			}
			verify_input_constraint(expr, expr->string->data);
			continue;

		case 2: /* Expression */
			state = 0;
			if (!evaluate_expression(expr))
				return;
			continue;
		}
	} END_FOR_EACH_PTR(expr);

	FOR_EACH_PTR(stmt->asm_clobbers, expr) {
		if (!expr) {
			error(stmt->pos, "bad asm output");
			return;
		}
		if (expr->type == EXPR_STRING)
			continue;
		error(expr->pos, "asm clobber is not a string");
	} END_FOR_EACH_PTR(expr);
}

static void evaluate_case_statement(struct statement *stmt)
{
	evaluate_expression(stmt->case_expression);
	evaluate_expression(stmt->case_to);
	evaluate_statement(stmt->case_statement);
}

static void check_case_type(struct expression *switch_expr, struct expression *case_expr)
{
	struct symbol *switch_type, *case_type;
	if (!case_expr)
		return;
	switch_type = switch_expr->ctype;
	case_type = case_expr->ctype;

	/* Both integer types? */
	if (is_int_type(switch_type) && is_int_type(case_type))
		return;
	if (compatible_restricted_binop(SPECIAL_EQUAL, &switch_expr, &case_expr))
		return;

	error(case_expr->pos, "incompatible types for 'case' statement");
}

static void evaluate_switch_statement(struct statement *stmt)
{
	struct symbol *sym;

	evaluate_expression(stmt->switch_expression);
	evaluate_statement(stmt->switch_statement);
	if (!stmt->switch_expression)
		return;
	FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
		struct statement *case_stmt = sym->stmt;
		check_case_type(stmt->switch_expression, case_stmt->case_expression);
		check_case_type(stmt->switch_expression, case_stmt->case_to);
	} END_FOR_EACH_PTR(sym);
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
		evaluate_switch_statement(stmt);
		return NULL;
	case STMT_CASE:
		evaluate_case_statement(stmt);
		return NULL;
	case STMT_LABEL:
		return evaluate_statement(stmt->label_statement);
	case STMT_GOTO:
		evaluate_expression(stmt->goto_expression);
		return NULL;
	case STMT_NONE:
		break;
	case STMT_ASM:
		evaluate_asm_statement(stmt);
		return NULL;
	case STMT_CONTEXT:
		evaluate_expression(stmt->expression);
		return NULL;
	case STMT_RANGE:
		evaluate_expression(stmt->range_expression);
		evaluate_expression(stmt->range_low);
		evaluate_expression(stmt->range_high);
		return NULL;
	}
	return NULL;
}
