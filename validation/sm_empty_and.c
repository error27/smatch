#include "check_debug.h"

int test(int x)
{
	int left, right;

	if (x != -28)
		return 0;

	if (x != -28 && x != -30) {
		left = x & 1;
		right = 1 & x;
		__smatch_implied(left);
		__smatch_implied(right);
	}

	return 0;
}

/*
 * check-name: smatch: empty range bitwise AND
 * check-command: smatch -I.. sm_empty_and.c
 *
 * check-output-start
sm_empty_and.c:13 test() implied: left = 's32min-s32max'
sm_empty_and.c:14 test() implied: right = 's32min-s32max'
 * check-output-end
 */
