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
		expression_error(expr, "undefined identifier '%s'", show_ident(expr->symbol_name));
		return NULL;
	}

	examine_symbol_type(sym);

	base_type = get_base_type(sym);
	if (!base_type) {
		expression_error(expr, "identifier '%s' has no type", show_ident(expr->symbol_name));
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
	sym->string = 1;
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
	struct symbol *orig_type = type;
	unsigned long mod =  type->ctype.modifiers;
	int width;

	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type->type == SYM_ENUM)
		type = type->ctype.base_type;
	width = type->bit_size;

	/*
	 * Bitfields always promote to the base type,
	 * even if the bitfield might be bigger than
	 * an "int".
	 */
	if (type->type == SYM_BITFIELD) {
		type = type->ctype.base_type;
		orig_type = type;
	}
	mod = type->ctype.modifiers;
	if (width < bits_in_int)
		return &int_ctype;

	/* If char/short has as many bits as int, it still gets "promoted" */
	if (mod & (MOD_CHAR | MOD_SHORT)) {
		if (mod & MOD_UNSIGNED)
			return &uint_ctype;
		return &int_ctype;
	}
	return orig_type;
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
	return orig->bit_size == new->bit_size && orig->bit_offset == new->bit_offset;
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

static void
warn_for_different_enum_types (struct position pos,
			       struct symbol *typea,
			       struct symbol *typeb)
{
	if (!Wenum_mismatch)
		return;
	if (typea->type == SYM_NODE)
		typea = typea->ctype.base_type;
	if (typeb->type == SYM_NODE)
		typeb = typeb->ctype.base_type;

	if (typea == typeb)
		return;

	if (typea->type == SYM_ENUM && typeb->type == SYM_ENUM) {
		warning(pos, "mixing different enum types");
		info(pos, "    %s versus", show_typename(typea));
		info(pos, "    %s", show_typename(typeb));
	}
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

	warn_for_different_enum_types (old->pos, old->ctype, type);

	if (is_same_type(old, type))
		return old;

	/*
	 * See if we can simplify the op. Move the cast down.
	 */
	switch (old->type) {
	case EXPR_PREOP:
		if (old->ctype->bit_size < type->bit_size)
			break;
		if (old->op == '~') {
			old->ctype = type;
			old->unop = cast_to(old->unop, type);
			return old;
		}
		break;

	case EXPR_IMPLIED_CAST:
		warn_for_different_enum_types(old->pos, old->ctype, type);

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

enum {
	TYPE_NUM = 1,
	TYPE_BITFIELD = 2,
	TYPE_RESTRICT = 4,
	TYPE_FLOAT = 8,
	TYPE_PTR = 16,
	TYPE_COMPOUND = 32,
	TYPE_FOULED = 64,
};

static inline int classify_type(struct symbol *type, struct symbol **base)
{
	static int type_class[SYM_BAD + 1] = {
		[SYM_PTR] = TYPE_PTR,
		[SYM_FN] = TYPE_PTR,
		[SYM_ARRAY] = TYPE_PTR | TYPE_COMPOUND,
		[SYM_STRUCT] = TYPE_COMPOUND,
		[SYM_UNION] = TYPE_COMPOUND,
		[SYM_BITFIELD] = TYPE_NUM | TYPE_BITFIELD,
		[SYM_RESTRICT] = TYPE_NUM | TYPE_RESTRICT,
		[SYM_FOULED] = TYPE_NUM | TYPE_RESTRICT | TYPE_FOULED,
	};
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type->type == SYM_ENUM)
		type = type->ctype.base_type;
	*base = type;
	if (type->type == SYM_BASETYPE) {
		if (type->ctype.base_type == &int_type)
			return TYPE_NUM;
		if (type->ctype.base_type == &fp_type)
			return TYPE_NUM | TYPE_FLOAT;
	}
	return type_class[type->type];
}

static inline int is_string_type(struct symbol *type)
{
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	return type->type == SYM_ARRAY && is_byte_type(type->ctype.base_type);
}

static struct symbol *bad_expr_type(struct expression *expr)
{
	sparse_error(expr->pos, "incompatible types for operation (%s)", show_special(expr->op));
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

	return expr->ctype = &bad_ctype;
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
		case '=':
		case SPECIAL_AND_ASSIGN:
		case SPECIAL_OR_ASSIGN:
		case SPECIAL_XOR_ASSIGN:
			return 1;	/* unfoul */
		case '|':
		case '^':
		case '?':
			return 2;	/* keep fouled */
		case SPECIAL_EQUAL:
		case SPECIAL_NOTEQUAL:
			return 3;	/* warn if fouled */
		default:
			return 0;	/* warn */
	}
}

static int restricted_unop(int op, struct symbol **type)
{
	if (op == '~') {
		if ((*type)->bit_size < bits_in_int)
			*type = befoul(*type);
		return 0;
	} if (op == '+')
		return 0;
	return 1;
}

static struct symbol *restricted_binop_type(int op,
					struct expression *left,
					struct expression *right,
					int lclass, int rclass,
					struct symbol *ltype,
					struct symbol *rtype)
{
	struct symbol *ctype = NULL;
	if (lclass & TYPE_RESTRICT) {
		if (rclass & TYPE_RESTRICT) {
			if (ltype == rtype) {
				ctype = ltype;
			} else if (lclass & TYPE_FOULED) {
				if (ltype->ctype.base_type == rtype)
					ctype = ltype;
			} else if (rclass & TYPE_FOULED) {
				if (rtype->ctype.base_type == ltype)
					ctype = rtype;
			}
		} else {
			if (!restricted_value(right, ltype))
				ctype = ltype;
		}
	} else if (!restricted_value(left, rtype))
		ctype = rtype;

	if (ctype) {
		switch (restricted_binop(op, ctype)) {
		case 1:
			if ((lclass ^ rclass) & TYPE_FOULED)
				ctype = ctype->ctype.base_type;
			break;
		case 3:
			if (!(lclass & rclass & TYPE_FOULED))
				break;
		case 0:
			ctype = NULL;
		default:
			break;
		}
	}

	return ctype;
}

static struct symbol *usual_conversions(int op,
					struct expression *left,
					struct expression *right,
					int lclass, int rclass,
					struct symbol *ltype,
					struct symbol *rtype)
{
	struct symbol *ctype;

	warn_for_different_enum_types(right->pos, left->ctype, right->ctype);

	if ((lclass | rclass) & TYPE_RESTRICT)
		goto Restr;

Normal:
	if (!(lclass & TYPE_FLOAT)) {
		if (!(rclass & TYPE_FLOAT))
			ctype = bigger_int_type(ltype, rtype);
		else
			ctype = rtype;
	} else if (rclass & TYPE_FLOAT) {
		unsigned long lmod = ltype->ctype.modifiers;
		unsigned long rmod = rtype->ctype.modifiers;
		if (rmod & ~lmod & (MOD_LONG | MOD_LONGLONG))
			ctype = rtype;
		else
			ctype = ltype;
	} else
		ctype = ltype;

Convert:
	return ctype;

Restr:
	ctype = restricted_binop_type(op, left, right,
				      lclass, rclass, ltype, rtype);
	if (ctype)
		goto Convert;

	if (lclass & TYPE_RESTRICT) {
		warning(left->pos, "restricted degrades to integer");
		if (lclass & TYPE_FOULED)
			ltype = ltype->ctype.base_type;
		ltype = ltype->ctype.base_type;
	}
	if (rclass & TYPE_RESTRICT) {
		warning(right->pos, "restricted degrades to integer");
		if (rclass & TYPE_FOULED)
			rtype = rtype->ctype.base_type;
		rtype = rtype->ctype.base_type;
	}
	goto Normal;
}

static struct symbol *evaluate_arith(struct expression *expr, int float_ok)
{
	struct symbol *ltype, *rtype;
	int lclass = classify_type(expr->left->ctype, &ltype);
	int rclass = classify_type(expr->right->ctype, &rtype);
	struct symbol *ctype;

	if (!(lclass & rclass & TYPE_NUM))
		goto Bad;

	if (!float_ok && (lclass | rclass) & TYPE_FLOAT)
		goto Bad;

	ctype = usual_conversions(expr->op, expr->left, expr->right,
				  lclass, rclass, ltype, rtype);
	expr->left = cast_to(expr->left, ctype);
	expr->right = cast_to(expr->right, ctype);
	expr->ctype = ctype;
	return ctype;

Bad:
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

static inline int want_int(struct expression **expr, struct symbol **ctype)
{
	int class = classify_type((*expr)->ctype, ctype);

	if (!(class & TYPE_NUM))
		return 0;
	if (!(class & TYPE_RESTRICT))
		return 1;
	warning((*expr)->pos, "restricted degrades to integer");
	if (class & TYPE_FOULED)	/* unfoul it first */
		(*ctype) = (*ctype)->ctype.base_type;
	(*ctype) = (*ctype)->ctype.base_type;	/* get to arithmetic type */
	*expr = cast_to(*expr, *ctype);
	return 1;
}

static struct symbol *evaluate_ptr_add(struct expression *expr, struct symbol *ctype, struct expression **ip)
{
	struct expression *i = *ip;
	struct symbol *itype;
	int bit_size;

	if (!want_int(&i, &itype))
		return bad_expr_type(expr);

	examine_symbol_type(ctype);

	if (!ctype->ctype.base_type) {
		expression_error(expr, "missing type information");
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
		if (Waddress_space && as1 != as2)
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
				 * Warn if both are explicitly signed ("unsigned" is obviously
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
				const char *diffstr;
				diffstr = type_difference(arg1, arg2, 0, 0);
				if (diffstr) {
					static char argdiff[80];
					sprintf(argdiff, "incompatible argument %d (%s)", i, diffstr);
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
	if (Wnon_pointer_null && !is_ptr_type(expr->ctype))
		warning(expr->pos, "Using plain integer as NULL pointer");
	return 1;
}

/*
 * Ignore differences in "volatile" and "const"ness when
 * subtracting pointers
 */
#define MOD_IGN (MOD_VOLATILE | MOD_CONST)

static struct symbol *evaluate_ptr_sub(struct expression *expr, struct expression *l)
{
	const char *typediff;
	struct symbol *ctype;
	struct symbol *ltype, *rtype;
	struct expression *r = expr->right;

	ltype = degenerate(l);
	rtype = degenerate(r);

	/*
	 * If it is an integer subtract: the ptr add case will do the
	 * right thing.
	 */
	if (!is_ptr_type(rtype))
		return evaluate_ptr_add(expr, degenerate(l), &expr->right);

	ctype = ltype;
	typediff = type_difference(ltype, rtype, ~MOD_SIZE, ~MOD_SIZE);
	if (typediff)
		expression_error(expr, "subtraction of different types can't work (%s)", typediff);
	examine_symbol_type(ctype);

	/* Figure out the base type we point to */
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (ctype->type != SYM_PTR && ctype->type != SYM_ARRAY) {
		expression_error(expr, "subtraction of functions? Share your drugs");
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
		return evaluate_ptr_sub(expr, left);

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
	struct symbol *ltype, *rtype;

	if (want_int(&expr->left, &ltype) && want_int(&expr->right, &rtype)) {
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

	ctype = evaluate_arith(expr, 1);
	if (ctype) {
		if (ctype->ctype.modifiers & MOD_UNSIGNED)
			expr->op = modify_for_unsigned(expr->op);
	}
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
 * for testing, that's OK.
 */
static struct symbol *evaluate_conditional_expression(struct expression *expr)
{
	struct expression **true;
	struct symbol *ctype, *ltype, *rtype;
	int lclass, rclass;
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

	lclass = classify_type(ltype, &ltype);
	rclass = classify_type(rtype, &rtype);
	if (lclass & rclass & TYPE_NUM) {
		ctype = usual_conversions('?', *true, expr->cond_false,
					  lclass, rclass, ltype, rtype);
		*true = cast_to(*true, ctype);
		expr->cond_false = cast_to(expr->cond_false, ctype);
		goto out;
	}
	ctype = compatible_ptr_type(*true, expr->cond_false);
	if (ctype)
		goto out;
	ctype = ltype;
	typediff = type_difference(ltype, rtype, MOD_IGN, MOD_IGN);
	if (!typediff)
		goto out;
	expression_error(expr, "incompatible types in conditional expression (%s)", typediff);
	return NULL;

out:
	expr->ctype = ctype;
	return ctype;
}

/* FP assignments can not do modulo or bit operations */
static int compatible_float_op(int op)
{
	return	op == SPECIAL_ADD_ASSIGN ||
		op == SPECIAL_SUB_ASSIGN ||
		op == SPECIAL_MUL_ASSIGN ||
		op == SPECIAL_DIV_ASSIGN;
}

static int evaluate_assign_op(struct expression *expr)
{
	struct symbol *target = expr->left->ctype;
	struct symbol *source = expr->right->ctype;
	struct symbol *t, *s;
	int tclass = classify_type(target, &t);
	int sclass = classify_type(source, &s);
	int op = expr->op;

	if (tclass & sclass & TYPE_NUM) {
		if (tclass & TYPE_FLOAT && !compatible_float_op(op)) {
			expression_error(expr, "invalid assignment");
			return 0;
		}
		if (tclass & TYPE_RESTRICT) {
			if (!restricted_binop(op, t)) {
				expression_error(expr, "bad restricted assignment");
				return 0;
			}
			/* allowed assignments unfoul */
			if (sclass & TYPE_FOULED && s->ctype.base_type == t)
				goto Cast;
			if (!restricted_value(expr->right, t))
				return 1;
		} else if (!(sclass & TYPE_RESTRICT))
			goto Cast;
		/* source and target would better be identical restricted */
		if (t == s)
			return 1;
		warning(expr->pos, "invalid restricted assignment");
		expr->right = cast_to(expr->right, target);
		return 0;
	}
	if (tclass & TYPE_PTR) {
		if (op == SPECIAL_ADD_ASSIGN || op == SPECIAL_SUB_ASSIGN) {
			evaluate_ptr_add(expr, target, &expr->right);
			return 1;
		}
		expression_error(expr, "invalid pointer assignment");
		return 0;
	}

	expression_error(expr, "invalid assignment");
	return 0;

Cast:
	expr->right = cast_to(expr->right, target);
	return 1;
}

static int compatible_assignment_types(struct expression *expr, struct symbol *target,
	struct expression **rp, struct symbol *source, const char *where)
{
	const char *typediff;
	struct symbol *t, *s;
	int target_as;
	int tclass = classify_type(target, &t);
	int sclass = classify_type(source, &s);

	if (tclass & sclass & TYPE_NUM) {
		if (tclass & TYPE_RESTRICT) {
			/* allowed assignments unfoul */
			if (sclass & TYPE_FOULED && s->ctype.base_type == t)
				goto Cast;
			if (!restricted_value(*rp, target))
				return 1;
		} else if (!(sclass & TYPE_RESTRICT))
			goto Cast;
	}

	/* It's OK if the target is more volatile or const than the source */
	typediff = type_difference(target, source, MOD_VOLATILE | MOD_CONST, 0);
	if (!typediff)
		return 1;

	/* Pointer destination? */
	if (tclass & TYPE_PTR) {
		struct expression *right = *rp;
		int source_as;

		// NULL pointer is always OK
		if (is_null_ptr(right))
			goto Cast;

		/* "void *" matches anything as long as the address space is OK */
		target_as = t->ctype.as | target->ctype.as;
		source_as = s->ctype.as | source->ctype.as;
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
		expression_error(left, "assignment to const expression");

	/* We know left is an lvalue, so it's a "preop-*" */
	mark_assigned(left->unop);
}

static struct symbol *evaluate_assignment(struct expression *expr)
{
	struct expression *left = expr->left, *right = expr->right;
	struct expression *where = expr;
	struct symbol *ltype, *rtype;

	if (!lvalue_expression(left)) {
		expression_error(expr, "not an lvalue");
		return NULL;
	}

	ltype = left->ctype;

	if (expr->op != '=') {
		if (!evaluate_assign_op(expr))
			return NULL;
	} else {
		rtype = degenerate(right);
		if (!compatible_assignment_types(where, ltype, &where->right, rtype, "assignment"))
			return NULL;
	}

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
				ptr->ctype.modifiers |= s->ctype.modifiers & MOD_PTRINHERIT;

				s->ctype.base_type = ptr;
				s->ctype.as = 0;
				s->ctype.modifiers &= ~MOD_PTRINHERIT;
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
		ptr->ctype.modifiers |= sym->ctype.modifiers & MOD_PTRINHERIT;
		sym = sym->ctype.base_type;
	}
	if (degenerate && sym->type == SYM_ARRAY) {
		ptr->ctype.as |= sym->ctype.as;
		ptr->ctype.modifiers |= sym->ctype.modifiers & MOD_PTRINHERIT;
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
			expression_error(expr, "strange non-value function or array");
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
		expression_error(expr, "not addressable");
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
		expression_error(expr, "cannot dereference this type");
		return NULL;
	case SYM_PTR:
		node->ctype.modifiers = target->ctype.modifiers & MOD_SPECIFIER;
		merge_type(node, ctype);
		break;

	case SYM_ARRAY:
		if (!lvalue_expression(op)) {
			expression_error(op, "non-lvalue array??");
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
		expression_error(expr, "need lvalue expression for ++/--");
		return NULL;
	}
	if (is_restricted_type(ctype) && restricted_unop(expr->op, &ctype)) {
		expression_error(expr, "bad operation on restricted");
		return NULL;
	} else if (is_fouled_type(ctype) && restricted_unop(expr->op, &ctype)) {
		expression_error(expr, "bad operation on restricted");
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
	} else if (is_restricted_type(ctype) && !restricted_unop(expr->op, &ctype)) {
		/* no conversions needed */
	} else if (is_fouled_type(ctype) && !restricted_unop(expr->op, &ctype)) {
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
		 * From a type evaluation standpoint the preops are
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
		} else if (is_fouled_type(ctype)) {
			warning(expr->pos, "restricted degrades to integer");
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
		expression_error(expr, "bad member name");
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
		expression_error(expr, "expected structure or union");
		return NULL;
	}
	examine_symbol_type(ctype);
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
		if (ctype->symbol_list)
			expression_error(expr, "no member '%s' in %s %.*s",
				show_ident(ident), type, namelen, name);
		else
			expression_error(expr, "using member '%s' in "
				"incomplete %s %.*s", show_ident(ident),
				type, namelen, name);
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
		} else if (is_fouled_type(sym)) {
			sym = &int_ctype;
		}
	}
	examine_symbol_type(sym);
	if (is_bitfield_type(sym)) {
		expression_error(expr, "trying to examine bitfield type");
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
		expression_error(expr, "cannot size expression");
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
		expression_error(expr, "expected pointer expression");
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

static struct symbol *find_struct_ident(struct symbol *ctype, struct ident *ident)
{
	struct symbol *sym;

	FOR_EACH_PTR(ctype->symbol_list, sym) {
		if (sym->ident == ident)
			return sym;
	} END_FOR_EACH_PTR(sym);
	return NULL;
}

static void convert_index(struct expression *e)
{
	struct expression *child = e->idx_expression;
	unsigned from = e->idx_from;
	unsigned to = e->idx_to + 1;
	e->type = EXPR_POS;
	e->init_offset = from * (e->ctype->bit_size>>3);
	e->init_nr = to - from;
	e->init_expr = child;
}

static void convert_ident(struct expression *e)
{
	struct expression *child = e->ident_expression;
	struct symbol *sym = e->field;
	e->type = EXPR_POS;
	e->init_offset = sym->offset;
	e->init_nr = 1;
	e->init_expr = child;
}

static void convert_designators(struct expression *e)
{
	while (e) {
		if (e->type == EXPR_INDEX)
			convert_index(e);
		else if (e->type == EXPR_IDENTIFIER)
			convert_ident(e);
		else
			break;
		e = e->init_expr;
	}
}

static void excess(struct expression *e, const char *s)
{
	warning(e->pos, "excessive elements in %s initializer", s);
}

/*
 * implicit designator for the first element
 */
static struct expression *first_subobject(struct symbol *ctype, int class,
					  struct expression **v)
{
	struct expression *e = *v, *new;

	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;

	if (class & TYPE_PTR) { /* array */
		if (!ctype->bit_size)
			return NULL;
		new = alloc_expression(e->pos, EXPR_INDEX);
		new->idx_expression = e;
		new->ctype = ctype->ctype.base_type;
	} else  {
		struct symbol *field, *p;
		PREPARE_PTR_LIST(ctype->symbol_list, p);
		while (p && !p->ident && is_bitfield_type(p))
			NEXT_PTR_LIST(p);
		field = p;
		FINISH_PTR_LIST(p);
		if (!field)
			return NULL;
		new = alloc_expression(e->pos, EXPR_IDENTIFIER);
		new->ident_expression = e;
		new->field = new->ctype = field;
	}
	*v = new;
	return new;
}

/*
 * sanity-check explicit designators; return the innermost one or NULL
 * in case of error.  Assign types.
 */
static struct expression *check_designators(struct expression *e,
					    struct symbol *ctype)
{
	struct expression *last = NULL;
	const char *err;
	while (1) {
		if (ctype->type == SYM_NODE)
			ctype = ctype->ctype.base_type;
		if (e->type == EXPR_INDEX) {
			struct symbol *type;
			if (ctype->type != SYM_ARRAY) {
				err = "array index in non-array";
				break;
			}
			type = ctype->ctype.base_type;
			if (ctype->bit_size >= 0 && type->bit_size >= 0) {
				unsigned offset = e->idx_to * type->bit_size;
				if (offset >= ctype->bit_size) {
					err = "index out of bounds in";
					break;
				}
			}
			e->ctype = ctype = type;
			ctype = type;
			last = e;
			if (!e->idx_expression) {
				err = "invalid";
				break;
			}
			e = e->idx_expression;
		} else if (e->type == EXPR_IDENTIFIER) {
			if (ctype->type != SYM_STRUCT && ctype->type != SYM_UNION) {
				err = "field name not in struct or union";
				break;
			}
			ctype = find_struct_ident(ctype, e->expr_ident);
			if (!ctype) {
				err = "unknown field name in";
				break;
			}
			e->field = e->ctype = ctype;
			last = e;
			if (!e->ident_expression) {
				err = "invalid";
				break;
			}
			e = e->ident_expression;
		} else if (e->type == EXPR_POS) {
			err = "internal front-end error: EXPR_POS in";
			break;
		} else
			return last;
	}
	expression_error(e, "%s initializer", err);
	return NULL;
}

/*
 * choose the next subobject to initialize.
 *
 * Get designators for next element, switch old ones to EXPR_POS.
 * Return the resulting expression or NULL if we'd run out of subobjects.
 * The innermost designator is returned in *v.  Designators in old
 * are assumed to be already sanity-checked.
 */
static struct expression *next_designators(struct expression *old,
			     struct symbol *ctype,
			     struct expression *e, struct expression **v)
{
	struct expression *new = NULL;

	if (!old)
		return NULL;
	if (old->type == EXPR_INDEX) {
		struct expression *copy;
		unsigned n;

		copy = next_designators(old->idx_expression,
					old->ctype, e, v);
		if (!copy) {
			n = old->idx_to + 1;
			if (n * old->ctype->bit_size == ctype->bit_size) {
				convert_index(old);
				return NULL;
			}
			copy = e;
			*v = new = alloc_expression(e->pos, EXPR_INDEX);
		} else {
			n = old->idx_to;
			new = alloc_expression(e->pos, EXPR_INDEX);
		}

		new->idx_from = new->idx_to = n;
		new->idx_expression = copy;
		new->ctype = old->ctype;
		convert_index(old);
	} else if (old->type == EXPR_IDENTIFIER) {
		struct expression *copy;
		struct symbol *field;

		copy = next_designators(old->ident_expression,
					old->ctype, e, v);
		if (!copy) {
			field = old->field->next_subobject;
			if (!field) {
				convert_ident(old);
				return NULL;
			}
			copy = e;
			*v = new = alloc_expression(e->pos, EXPR_IDENTIFIER);
		} else {
			field = old->field;
			new = alloc_expression(e->pos, EXPR_IDENTIFIER);
		}

		new->field = field;
		new->expr_ident = field->ident;
		new->ident_expression = copy;
		new->ctype = field;
		convert_ident(old);
	}
	return new;
}

static int handle_simple_initializer(struct expression **ep, int nested,
				     int class, struct symbol *ctype);

/*
 * deal with traversing subobjects [6.7.8(17,18,20)]
 */
static void handle_list_initializer(struct expression *expr,
				    int class, struct symbol *ctype)
{
	struct expression *e, *last = NULL, *top = NULL, *next;
	int jumped = 0;

	FOR_EACH_PTR(expr->expr_list, e) {
		struct expression **v;
		struct symbol *type;
		int lclass;

		if (e->type != EXPR_INDEX && e->type != EXPR_IDENTIFIER) {
			if (!top) {
				top = e;
				last = first_subobject(ctype, class, &top);
			} else {
				last = next_designators(last, ctype, e, &top);
			}
			if (!last) {
				excess(e, class & TYPE_PTR ? "array" :
							"struct or union");
				DELETE_CURRENT_PTR(e);
				continue;
			}
			if (jumped) {
				warning(e->pos, "advancing past deep designator");
				jumped = 0;
			}
			REPLACE_CURRENT_PTR(e, last);
		} else {
			next = check_designators(e, ctype);
			if (!next) {
				DELETE_CURRENT_PTR(e);
				continue;
			}
			top = next;
			/* deeper than one designator? */
			jumped = top != e;
			convert_designators(last);
			last = e;
		}

found:
		lclass = classify_type(top->ctype, &type);
		if (top->type == EXPR_INDEX)
			v = &top->idx_expression;
		else
			v = &top->ident_expression;

		if (handle_simple_initializer(v, 1, lclass, top->ctype))
			continue;

		if (!(lclass & TYPE_COMPOUND)) {
			warning(e->pos, "bogus scalar initializer");
			DELETE_CURRENT_PTR(e);
			continue;
		}

		next = first_subobject(type, lclass, v);
		if (next) {
			warning(e->pos, "missing braces around initializer");
			top = next;
			goto found;
		}

		DELETE_CURRENT_PTR(e);
		excess(e, lclass & TYPE_PTR ? "array" : "struct or union");

	} END_FOR_EACH_PTR(e);

	convert_designators(last);
	expr->ctype = ctype;
}

static int is_string_literal(struct expression **v)
{
	struct expression *e = *v;
	while (e && e->type == EXPR_PREOP && e->op == '(')
		e = e->unop;
	if (!e || e->type != EXPR_STRING)
		return 0;
	if (e != *v && Wparen_string)
		warning(e->pos,
			"array initialized from parenthesized string constant");
	*v = e;
	return 1;
}

/*
 * We want a normal expression, possibly in one layer of braces.  Warn
 * if the latter happens inside a list (it's legal, but likely to be
 * an effect of screwup).  In case of anything not legal, we are definitely
 * having an effect of screwup, so just fail and let the caller warn.
 */
static struct expression *handle_scalar(struct expression *e, int nested)
{
	struct expression *v = NULL, *p;
	int count = 0;

	/* normal case */
	if (e->type != EXPR_INITIALIZER)
		return e;

	FOR_EACH_PTR(e->expr_list, p) {
		if (!v)
			v = p;
		count++;
	} END_FOR_EACH_PTR(p);
	if (count != 1)
		return NULL;
	switch(v->type) {
	case EXPR_INITIALIZER:
	case EXPR_INDEX:
	case EXPR_IDENTIFIER:
		return NULL;
	default:
		break;
	}
	if (nested)
		warning(e->pos, "braces around scalar initializer");
	return v;
}

/*
 * deal with the cases that don't care about subobjects:
 * scalar <- assignment expression, possibly in braces [6.7.8(11)]
 * character array <- string literal, possibly in braces [6.7.8(14)]
 * struct or union <- assignment expression of compatible type [6.7.8(13)]
 * compound type <- initializer list in braces [6.7.8(16)]
 * The last one punts to handle_list_initializer() which, in turn will call
 * us for individual elements of the list.
 *
 * We do not handle 6.7.8(15) (wide char array <- wide string literal) for
 * the lack of support of wide char stuff in general.
 *
 * One note: we need to take care not to evaluate a string literal until
 * we know that we *will* handle it right here.  Otherwise we would screw
 * the cases like struct { struct {char s[10]; ...} ...} initialized with
 * { "string", ...} - we need to preserve that string literal recognizable
 * until we dig into the inner struct.
 */
static int handle_simple_initializer(struct expression **ep, int nested,
				     int class, struct symbol *ctype)
{
	int is_string = is_string_type(ctype);
	struct expression *e = *ep, *p;
	struct symbol *type;

	if (!e)
		return 0;

	/* scalar */
	if (!(class & TYPE_COMPOUND)) {
		e = handle_scalar(e, nested);
		if (!e)
			return 0;
		*ep = e;
		type = evaluate_expression(e);
		if (!e->ctype)
			return 1;
		compatible_assignment_types(e, ctype, ep, degenerate(e),
					    "initializer");
		return 1;
	}

	/*
	 * sublist; either a string, or we dig in; the latter will deal with
	 * pathologies, so we don't need anything fancy here.
	 */
	if (e->type == EXPR_INITIALIZER) {
		if (is_string) {
			struct expression *v = NULL;
			int count = 0;

			FOR_EACH_PTR(e->expr_list, p) {
				if (!v)
					v = p;
				count++;
			} END_FOR_EACH_PTR(p);
			if (count == 1 && is_string_literal(&v)) {
				*ep = e = v;
				goto String;
			}
		}
		handle_list_initializer(e, class, ctype);
		return 1;
	}

	/* string */
	if (is_string_literal(&e)) {
		/* either we are doing array of char, or we'll have to dig in */
		if (is_string) {
			*ep = e;
			goto String;
		}
		return 0;
	}
	/* struct or union can be initialized by compatible */
	if (class != TYPE_COMPOUND)
		return 0;
	type = evaluate_expression(e);
	if (!type)
		return 0;
	if (ctype->type == SYM_NODE)
		ctype = ctype->ctype.base_type;
	if (type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (ctype == type)
		return 1;
	return 0;

String:
	p = alloc_expression(e->pos, EXPR_STRING);
	*p = *e;
	type = evaluate_expression(p);
	if (ctype->bit_size != -1 &&
	    ctype->bit_size + bits_in_char < type->bit_size) {
		warning(e->pos,
			"too long initializer-string for array of char");
	}
	*ep = p;
	return 1;
}

static void evaluate_initializer(struct symbol *ctype, struct expression **ep)
{
	struct symbol *type;
	int class = classify_type(ctype, &type);
	if (!handle_simple_initializer(ep, 0, class, ctype))
		expression_error(*ep, "invalid initializer");
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
	struct symbol *type = v->ctype;

	if (!Wcast_to_address_space)
		return;

	if (v->type != EXPR_VALUE || v->value)
		goto out;

	/* cast from constant 0 to pointer is OK */
	if (is_int_type(type))
		return;

	if (type->type == SYM_NODE)
		type = type->ctype.base_type;

	if (type->type == SYM_PTR && type->ctype.base_type == &void_ctype)
		return;

out:
	warning(e->pos, "cast adds address space to expression (<asn:%d>)", as);
}

static struct symbol *evaluate_cast(struct expression *expr)
{
	struct expression *target = expr->cast_expression;
	struct symbol *ctype = examine_symbol_type(expr->cast_type);
	struct symbol *t1, *t2;
	int class1, class2;
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

	class1 = classify_type(ctype, &t1);
	/*
	 * You can always throw a value away by casting to
	 * "void" - that's an implicit "force". Note that
	 * the same is _not_ true of "void *".
	 */
	if (t1 == &void_ctype)
		goto out;

	if (class1 & TYPE_COMPOUND)
		warning(expr->pos, "cast to non-scalar");

	t2 = target->ctype;
	if (!t2) {
		expression_error(expr, "cast from unknown type");
		goto out;
	}
	class2 = classify_type(t2, &t2);

	if (class2 & TYPE_COMPOUND)
		warning(expr->pos, "cast from non-scalar");

	/* allowed cast unfouls */
	if (class2 & TYPE_FOULED)
		t2 = t2->ctype.base_type;

	if (!(ctype->ctype.modifiers & MOD_FORCE) && t1 != t2) {
		if (class1 & TYPE_RESTRICT)
			warning(expr->pos, "cast to restricted type");
		if (class2 & TYPE_RESTRICT)
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

	examine_fn_arguments(ctype);
        if (sym->type == SYM_NODE && fn->type == EXPR_PREOP &&
	    sym->op && sym->op->args) {
		if (!sym->op->args(expr))
			return NULL;
	} else {
		if (!evaluate_arguments(sym, ctype, arglist))
			return NULL;
		if (ctype->type != SYM_FN) {
			expression_error(expr, "not a function %s",
				     show_ident(sym->ident));
			return NULL;
		}
		args = expression_list_size(expr->args);
		fnargs = symbol_list_size(ctype->arguments);
		if (args < fnargs)
			expression_error(expr,
				     "not enough arguments for function %s",
				     show_ident(sym->ident));
		if (args > fnargs && !ctype->variadic)
			expression_error(expr,
				     "too many arguments for function %s",
				     show_ident(sym->ident));
	}
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
		expression_error(expr, "value expression without a type");
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
		expression_error(expr, "internal front-end error: initializer in expression");
		return NULL;
	case EXPR_SLICE:
		expression_error(expr, "internal front-end error: SLICE re-evaluated");
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
			sparse_error(sym->pos, "symbol '%s' redeclared with different type (originally declared at %s:%d) - %s",
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
		if (!Wdecl)
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
	if (sym->evaluated)
		return sym;
	sym->evaluated = 1;

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
		evaluate_symbol(sym);
		check_duplicates(sym);
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
			expression_error(expr, "return expression in %s function", fntype?"void":"typeless");
		return NULL;
	}

	if (!expr) {
		sparse_error(stmt->pos, "return with no return value");
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
		expression_error(expr, "output constraint is not an assignment constraint (\"%s\")", constraint);
	}
}

static void verify_input_constraint(struct expression *expr, const char *constraint)
{
	switch (*constraint) {
	case '=':	/* Assignment */
	case '+':	/* Update */
		expression_error(expr, "input constraint with assignment (\"%s\")", constraint);
	}
}

static void evaluate_asm_statement(struct statement *stmt)
{
	struct expression *expr;
	int state;

	expr = stmt->asm_string;
	if (!expr || expr->type != EXPR_STRING) {
		sparse_error(stmt->pos, "need constant string for inline asm");
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
				sparse_error(expr ? expr->pos : stmt->pos, "asm output constraint is not a string");
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
				sparse_error(expr ? expr->pos : stmt->pos, "asm input constraint is not a string");
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
			sparse_error(stmt->pos, "bad asm output");
			return;
		}
		if (expr->type == EXPR_STRING)
			continue;
		expression_error(expr, "asm clobber is not a string");
	} END_FOR_EACH_PTR(expr);
}

static void evaluate_case_statement(struct statement *stmt)
{
	evaluate_expression(stmt->case_expression);
	evaluate_expression(stmt->case_to);
	evaluate_statement(stmt->case_statement);
}

static void check_case_type(struct expression *switch_expr,
			    struct expression *case_expr,
			    struct expression **enumcase)
{
	struct symbol *switch_type, *case_type;
	int sclass, cclass;

	if (!case_expr)
		return;

	switch_type = switch_expr->ctype;
	case_type = evaluate_expression(case_expr);

	if (!switch_type || !case_type)
		goto Bad;
	if (enumcase) {
		if (*enumcase)
			warn_for_different_enum_types(case_expr->pos, case_type, (*enumcase)->ctype);
		else if (is_enum_type(case_type))
			*enumcase = case_expr;
	}

	sclass = classify_type(switch_type, &switch_type);
	cclass = classify_type(case_type, &case_type);

	/* both should be arithmetic */
	if (!(sclass & cclass & TYPE_NUM))
		goto Bad;

	/* neither should be floating */
	if ((sclass | cclass) & TYPE_FLOAT)
		goto Bad;

	/* if neither is restricted, we are OK */
	if (!((sclass | cclass) & TYPE_RESTRICT))
		return;

	if (!restricted_binop_type(SPECIAL_EQUAL, case_expr, switch_expr,
				   cclass, sclass, case_type, switch_type))
		warning(case_expr->pos, "restricted degrades to integer");

	return;

Bad:
	expression_error(case_expr, "incompatible types for 'case' statement");
}

static void evaluate_switch_statement(struct statement *stmt)
{
	struct symbol *sym;
	struct expression *enumcase = NULL;
	struct expression **enumcase_holder = &enumcase;
	struct expression *sel = stmt->switch_expression;

	evaluate_expression(sel);
	evaluate_statement(stmt->switch_statement);
	if (!sel)
		return;
	if (sel->ctype && is_enum_type(sel->ctype))
		enumcase_holder = NULL; /* Only check cases against switch */

	FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
		struct statement *case_stmt = sym->stmt;
		check_case_type(sel, case_stmt->case_expression, enumcase_holder);
		check_case_type(sel, case_stmt->case_to, enumcase_holder);
	} END_FOR_EACH_PTR(sym);
}

struct symbol *evaluate_statement(struct statement *stmt)
{
	if (!stmt)
		return NULL;

	switch (stmt->type) {
	case STMT_DECLARATION: {
		struct symbol *s;
		FOR_EACH_PTR(stmt->declaration, s) {
			evaluate_symbol(s);
		} END_FOR_EACH_PTR(s);
		return NULL;
	}

	case STMT_RETURN:
		return evaluate_return_expression(stmt);

	case STMT_EXPRESSION:
		if (!evaluate_expression(stmt->expression))
			return NULL;
		return degenerate(stmt->expression);

	case STMT_COMPOUND: {
		struct statement *s;
		struct symbol *type = NULL;

		/* Evaluate the return symbol in the compound statement */
		evaluate_symbol(stmt->ret);

		/*
		 * Then, evaluate each statement, making the type of the
		 * compound statement be the type of the last statement
		 */
		type = evaluate_statement(stmt->args);
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
