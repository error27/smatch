/*
 * sparse/check_signed.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Check for things which are signed but probably should be unsigned.
 * 
 * Hm...  It seems like at this point in the processing, sparse makes all
 * bitfields unsigned.  Which is logical but not what GCC does.
 *
 */

#include "smatch.h"

static int my_id;

static unsigned long long max_size(struct symbol *base_type)
{
	/*
	  I wanted to say:

	  unsigned long long ret = 0xffffffffffffffff;

	  But gcc complained that was too large.  What am I doing wrong?
	  Oh well.  I expect most of the problems are with smaller 
	  values anyway 
	*/

	unsigned long long ret = 0xffffffff;
	int bits;

	bits = base_type->bit_size;
	if (base_type->ctype.modifiers & MOD_SIGNED)
		bits--;
	ret >>= (32 - bits);
	return ret;
}

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	long long val;
	long long max;

	sym = get_type(expr->left);
	if (!sym) {
		//sm_msg("could not get type");
		return;
	}
	if (sym->bit_size >= 32) /* max_val limits this */
		return;
	if (!get_implied_value(expr->right, &val))
		return;
	max = max_size(sym);
	if (max < val)
		sm_msg("error: value %lld can't fit into %lld", val, max);
}

void check_signed(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
