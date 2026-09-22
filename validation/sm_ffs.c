#include "check_debug.h"

void test(unsigned long long x)
{
	if (x == 0)
		__smatch_implied(__builtin_ffsll(x));
	if (x == 8)
		__smatch_implied(__builtin_ffsll(x));

	if (x < 8 || x > 15)
		return;
	__smatch_implied(__builtin_ffsll(x));
}

/*
 * check-name: smatch: builtin ffs ranges
 * check-command: smatch -I.. sm_ffs.c
 *
 * check-output-start
sm_ffs.c:6 test() implied: __builtin_ffsll(x) = '0'
sm_ffs.c:8 test() implied: __builtin_ffsll(x) = '4'
sm_ffs.c:12 test() implied: __builtin_ffsll(x) = '1-4'
 * check-output-end
 */
