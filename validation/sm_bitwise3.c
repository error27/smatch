#include "check_debug.h"

int test(unsigned int a, unsigned int b)
{
	if (!a)
		return;

	__smatch_implied(a & 0xfff);

	if (!(b & (1 << 8)))
		return;
	__smatch_implied(b & 0xfff);

	return 0;
}


/*
 * check-name: smatch: bitwise
 * check-command: smatch -I.. sm_bitwise3.c
 *
 * check-output-start
sm_bitwise3.c:8 test() implied: a & 4095 = '0-4095'
sm_bitwise3.c:12 test() implied: b & 4095 = '256-4095'
 * check-output-end
 */
