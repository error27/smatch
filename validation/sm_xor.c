#include "check_debug.h"

void test(unsigned int x, unsigned int y)
{
	if (x < 1 || x > 2)
		return;
	if (y < 1 || y > 2)
		return;
	__smatch_implied(x ^ y);
}

/*
 * check-name: smatch: bitwise XOR range
 * check-command: smatch -I.. sm_xor.c
 *
 * check-output-start
sm_xor.c:9 test() implied: x ^ y = '0-3'
 * check-output-end
 */
