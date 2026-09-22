#include "check_debug.h"

void test_shift_range(unsigned int left, unsigned int shift)
{
	if (left < 1 || left > 3)
		return;
	if (shift < 1 || shift > 2)
		return;

	__smatch_implied(left);
	__smatch_implied(shift);
	__smatch_implied(left << shift);
}

void test_wrapped_shift(unsigned int left)
{
	if (left < 2147483648U || left > 2147483649U)
		return;

	__smatch_implied(left << 1);
}

/*
 * check-name: smatch: left shift ranges
 * check-command: ./smatch -I.. sm_lshift.c
 *
 * check-output-start
sm_lshift.c:10 test_shift_range() implied: left = '1-3'
sm_lshift.c:11 test_shift_range() implied: shift = '1-2'
sm_lshift.c:12 test_shift_range() implied: left << shift = '0-u32max'
sm_lshift.c:20 test_wrapped_shift() implied: left << 1 = '0-u32max'
 * check-output-end
 */
