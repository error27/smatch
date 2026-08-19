#include "check_debug.h"

void test_bounds(unsigned int left, unsigned int right)
{
	if (left < 12 || left > 15)
		return;
	if (right < 10 || right > 11)
		return;

	__smatch_implied(left & right);
}

void test_range_lists(unsigned int left, unsigned int right)
{
	if (!((left >= 12 && left <= 15) ||
	      (left >= 32 && left <= 35)))
		return;
	if (!((right >= 10 && right <= 11) ||
	      (right >= 20 && right <= 21)))
		return;

	__smatch_implied(left & right);
}

/*
 * check-name: smatch: bitwise AND bounds
 * check-command: smatch -I.. sm_and_bounds.c
 *
 * check-output-start
sm_and_bounds.c:10 test_bounds() implied: left & right = '8-11'
sm_and_bounds.c:22 test_range_lists() implied: left & right = '0-5,8-11'
 * check-output-end
 */
