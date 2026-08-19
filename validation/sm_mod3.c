#include "check_debug.h"

void test_positive(int left, int right)
{
	int result;

	left = __smatch_rl("1-2,10-11,30-31");
	right = __smatch_rl("5-6,20-21,50-51");
	result = left % right;
	__smatch_implied(result);
}

/*
 * check-name: smatch mod ranges
 * check-command: smatch -I.. sm_mod3.c
 *
 * check-output-start
sm_mod3.c:10 test_positive() implied: result = '0-20,30-31'
 * check-output-end
 */
