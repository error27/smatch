#include "check_debug.h"

void test(unsigned long long x)
{
	if (x == 8)
		__smatch_implied(__builtin_clzg(x));

	if (x < 8 || x > 15)
		return;
	__smatch_implied(__builtin_clzg(x));
}

/*
 * check-name: smatch: builtin clzg ranges
 * check-command: smatch -I.. sm_clzg.c
 *
 * check-output-start
sm_clzg.c:6 test() implied: __builtin_clzg(x) = '60'
sm_clzg.c:10 test() implied: __builtin_clzg(x) = '60-64'
 * check-output-end
 */
