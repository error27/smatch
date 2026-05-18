#include "check_debug.h"

void kfree(void *p);
extern void *p;
int *alloc(int x);

int func(void)
{
	int *p;
	int x, i;

	__smatch_debug_passes();

	if (x < 0 || x > 100)
		return -1;

	p = alloc(x);
	if (!p)
		x = 0;

	for (i = 0; i < x; i++)
		__smatch_implied(p);

	return 0;
}


/*
 * check-name: smatch loops #8
 * check-command: smatch -I.. sm_loops8.c
 *
 * check-output-start
sm_loops8.c:22 func() implied: p = '1-u64max'
sm_loops8.c:22 func() implied: p = '1-u64max'
 * check-output-end
 */
