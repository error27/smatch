/*
 * smatch/smatch_sval.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 *
 * Basically the point of sval is that it can hold both ULLONG_MAX and
 * LLONG_MIN.  If it is an unsigned type then we use sval.uvalue or if it is
 * signed we use sval.value.
 *
 * I considered just using one bit to store whether the value was signed vs
 * unsigned but I think it might help to have the type information so we know
 * how to do type promotion.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

__ALLOCATOR(sval_t, "svals", sval);

sval_t *sval_alloc(sval_t sval)
{
	sval_t *ret;

	ret = __alloc_sval(0);
	*ret = sval;
	return ret;
}

sval_t *sval_alloc_permanent(sval_t sval)
{
	sval_t *ret;

	ret = malloc(sizeof(*ret));
	*ret = sval;
	return ret;
}

sval_t sval_blank(struct expression *expr)
{
	sval_t ret;

	ret.type = get_type(expr);
	if (!ret.type)
		ret.type = &llong_ctype;
	ret.value = 123456789;

	return ret;
}

sval_t sval_from_val(struct expression *expr, long long val)
{
	sval_t ret;

	ret = sval_blank(expr);
	ret.value = val;
	ret = sval_cast(ret, expr);

	return ret;
}

int sval_unsigned(sval_t sval)
{
	return type_unsigned(sval.type);
}

int sval_signed(sval_t sval)
{
	return !type_unsigned(sval.type);
}

int sval_bits(sval_t sval)
{
	if (!sval.type)
		return 32;
	return sval.type->bit_size;
}

int sval_is_min(sval_t sval)
{
	sval_t min = sval_type_min(sval.type);

	if (sval_unsigned(sval)) {
		if (sval.uvalue == 0)
			return 1;
		return 0;
	}
	/* return true for less than min as well */
	return (sval.value <= min.value);
}

int sval_is_max(sval_t sval)
{
	sval_t max = sval_type_max(sval.type);

	if (sval_unsigned(sval))
		return (sval.uvalue >= max.value);
	return (sval.value >= max.value);
}

static int sval_unsigned_big(sval_t sval)
{
	if (sval_unsigned(sval) && sval_bits(sval) >= 32)
		return 1;
	return 0;
}

/*
 * Casts the values and then does a compare.  Returns -1 if one is smaller, 0 if
 * they are the same and 1 if two is larger.
 */
int sval_cmp(sval_t one, sval_t two)
{
	if (sval_unsigned_big(one) || sval_unsigned_big(two)) {
		if (one.uvalue < two.uvalue)
			return -1;
		if (one.uvalue == two.uvalue)
			return 0;
		return 1;
	}
	/* fix me handle type promotion and unsigned values */
	if (one.value < two.value)
		return -1;
	if (one.value == two.value)
		return 0;
	return 1;
}

int sval_cmp_val(sval_t one, long long val)
{
	if (one.value < val)
		return -1;
	if (one.value == val)
		return 0;
	return 1;
}

sval_t sval_cast(sval_t sval, struct expression *expr)
{
	sval_t ret;

	ret = sval_blank(expr);
	switch (sval_bits(ret)) {
	case 8:
		if (sval_unsigned(ret))
			ret.value = (long long)(unsigned char)sval.value;
		else
			ret.value = (long long)(char)sval.value;
		break;
	case 16:
		if (sval_unsigned(ret))
			ret.value = (long long)(unsigned short)sval.value;
		else
			ret.value = (long long)(short)sval.value;
		break;
	case 32:
		if (sval_unsigned(ret))
			ret.value = (long long)(unsigned int)sval.value;
		else
			ret.value = (long long)(int)sval.value;
		break;
	default:
		ret.value = sval.value;
	}
	return ret;

}

sval_t sval_preop(sval_t sval, int op)
{
	switch (op) {
	case '!':
		sval.value = !sval.value;
		break;
	case '~':
		sval.value = ~sval.value;
		/* fixme: should probably cast this here */
		break;
	case '-':
		sval.value = -sval.value;
		break;
	}
	return sval;
}

sval_t sval_binop(sval_t left, int op, sval_t right)
{
	sval_t ret;

	/* fixme: these need to have proper type promotions */
	ret.type = left.type;
	switch (op) {
	case '*':
		ret.value =  left.value * right.value;
		break;
	case '/':
		if (right.value == 0) {
			sm_msg("internal error: %s: divide by zero", __func__);
			ret.value = 123456789;
		} else {
			ret.value = left.value / right.value;
		}
		break;
	case '+':
		ret.value = left.value + right.value;
		break;
	case '-':
		ret.value = left.value - right.value;
		break;
	case '%':
		if (right.value == 0) {
			sm_msg("internal error: %s: MOD by zero", __func__);
			ret.value = 123456789;
		} else {
			ret.value = left.value % right.value;
		}
		break;
	case '|':
		ret.value = left.value | right.value;
		break;
	case '&':
		ret.value = left.value & right.value;
		break;
	case SPECIAL_RIGHTSHIFT:
		ret.value = left.value >> right.value;
		break;
	case SPECIAL_LEFTSHIFT:
		ret.value = left.value << right.value;
		break;
	case '^':
		ret.value = left.value ^ right.value;
		break;
	default:
		sm_msg("internal error: %s: unhandled binop %s", __func__,
		       show_special(op));
		ret.value = 1234567;
	}
	return ret;
}

const char *sval_to_str(sval_t sval)
{
	char buf[30];

	if (sval_unsigned(sval) && sval.value == ULLONG_MAX)
		return "u64max";
	if (sval.value == LLONG_MAX)
		return "max"; // FIXME: should be s64max
	if (sval_unsigned(sval) && sval.value == UINT_MAX)
		return "u32max";
	if (sval.value == INT_MAX)
		return "s32max";
	if (sval_unsigned(sval) && sval.value == USHRT_MAX)
		return "u16max";

	if ((sval.type == &sshort_ctype || sval.type == &short_ctype) && sval.value == SHRT_MIN)
		return "s16min";
	if ((sval.type == &sint_ctype || sval.type == &int_ctype) && sval.value == INT_MIN)
		return "s32min";
	if (sval_signed(sval) && sval.value == LLONG_MIN)
		return "min";  // FIXME: should be s64min

	if (sval_unsigned(sval))
		snprintf(buf, sizeof(buf), "%llu", sval.value);
	else if (sval.value < 0)
		snprintf(buf, sizeof(buf), "(%lld)", sval.value);
	else
		snprintf(buf, sizeof(buf), "%lld", sval.value);

	return alloc_sname(buf);
}

/*
 * This function is for compatibility.  Eventually everything will use svals
 * and we can get rid of whole_range.max.
 */
long long sval_to_ll(sval_t sval)
{
	if (sval_unsigned(sval) && sval.value == ULLONG_MAX)
		return whole_range.max;
	return sval.value;
}

sval_t ll_to_sval(long long val)
{
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = val;
	return ret;
}

static void free_svals(struct symbol *sym)
{
	clear_sval_alloc();
}

void register_sval(int my_id)
{
	add_hook(&free_svals, END_FUNC_HOOK);
}
