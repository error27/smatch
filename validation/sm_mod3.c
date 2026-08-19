#include "check_debug.h"

void test_positive(int left, int right)
{
	int result;

	left = __smatch_rl("1-2,10-11,30-31");
	right = __smatch_rl("5-6,20-21,50-51");
	result = left % right;
	__smatch_implied(result);
}

void test_negative_right(int left, int right)
{
	int result;

	if (left < 10 || left > 11)
		return;
	if (right < -6 || right > -5)
		return;

	result = left % right;
	__smatch_implied(result);
}

void test_both_negative(int left, int right)
{
	int result;

	if (left < -11 || left > -10)
		return;
	if (right < -6 || right > -5)
		return;

	result = left % right;
	__smatch_implied(result);
}

/*
 * check-name: smatch mod ranges
 * check-command: smatch -I.. sm_mod3.c
 *
 * check-output-start
sm_mod3.c:10 test_positive() implied: result = '0-20,30-31'
sm_mod3.c:23 test_negative_right() implied: result = '0-5'
sm_mod3.c:36 test_both_negative() implied: result = '(-5)-0'
 * check-output-end
 */
