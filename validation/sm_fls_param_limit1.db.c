#include "check_debug.h"

static inline int fls(unsigned int x)
{
	return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}

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
