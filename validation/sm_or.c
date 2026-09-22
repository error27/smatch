#include "check_debug.h"

void test(unsigned int x, unsigned int y)
{
	if (x < 1 || x > 2)
		return;
	if (y != 1)
		return;
	__smatch_implied(x | y);
}

/*
 * check-name: smatch: bitwise OR range
 * check-command: smatch -I.. sm_or.c
 *
 * check-output-start
sm_or.c:9 test() implied: x | y = '1-3'
 * check-output-end
 */
