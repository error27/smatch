#include "check_debug.h"

void test_signed_shift(int left, unsigned long shift)
{
	if (left < -16 || left > -8)
		return;
	if (shift != 2)
		return;

	__smatch_implied(left >> shift);
}

void test_shift_range(unsigned int left, unsigned int shift)
{
	if (left < 16 || left > 31)
		return;
	if (shift < 1 || shift > 2)
		return;

	__smatch_implied(left >> shift);
}

/*
 * check-name: smatch: right shift ranges
 * check-command: ./smatch -I.. sm_rshift.c
 *
 * check-output-start
sm_rshift.c:10 test_signed_shift() implied: left >> shift = '(-4)-(-2)'
sm_rshift.c:20 test_shift_range() implied: left >> shift = '0-u32max'
 * check-output-end
 */
