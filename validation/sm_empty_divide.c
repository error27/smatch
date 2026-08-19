#include "check_debug.h"

int test(int x)
{
	if (x != -28)
		return 0;

	if (x != -28 && x != -30) {
		__smatch_implied(x / 2);
		__smatch_implied(2 / x);
	}

	return 0;
}

/*
 * check-name: smatch: empty range division
 * check-command: smatch -I.. sm_empty_divide.c
 *
 * check-output-start
sm_empty_divide.c:9 test() implied: x / 2 = 's32min-s32max'
sm_empty_divide.c:10 test() implied: 2 / x = 's32min-s32max'
 * check-output-end
 */
