#include "check_debug.h"

static int fls(unsigned int x);

struct cache;

static struct cache *kmalloc_slab(unsigned long size, struct cache **b)
{
	int index;

	if (size > 8192)
		return (void *)0;
	if (size <= 192)
		return (void *)0;

	index = fls(size - 1);
	__smatch_force_on();
	__smatch_implied(size);
	__smatch_force_off();
	return b[index];
}

/*
 * check-name: smatch: DB parameter limit after fls()
 * check-command: validation/smatch_fls_param_limit.sh sm_fls_param_limit1.c
 * check-known-to-fail
 *
 * check-output-start
slub.i:18 kmalloc_slab() implied: size = '193-8192'
slub.i:18 kmalloc_slab() implied: size = '193-8192'
 * check-output-end
 */
