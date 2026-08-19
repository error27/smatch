#include "check_debug.h"

unsigned long vmemmap_base;

int func(void *page)
{
	if (vmemmap_base != 18434359174734282752UL &&
	    vmemmap_base != 18446719884453740544UL)
		return 0;

	__smatch_implied(page - vmemmap_base);

	return 0;
}

/*
 * check-name: smatch: subtract #8
 * check-command: smatch -I.. sm_subtract8.c
 *
 * check-output-start
sm_subtract8.c:11 func() implied: page - vmemmap_base = '0-u64max'
 * check-output-end
 */
