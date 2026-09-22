#include "check_debug.h"

void test(unsigned long long x)
{
	if (x == 8)
		__smatch_implied(__builtin_ctzg(x));

	if (x < 8 || x > 15)
		return;
	__smatch_implied(__builtin_ctzg(x));
}

/*
 * check-name: smatch: builtin ctzg ranges
 * check-command: smatch -I.. sm_ctzg.c
 *
 * check-output-start
sm_ctzg.c:6 test() implied: __builtin_ctzg(x) = '3'
sm_ctzg.c:10 test() implied: __builtin_ctzg(x) = '0-64'
 * check-output-end
 */
