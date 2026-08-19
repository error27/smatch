#include "check_debug.h"

int test(int x)
{
	if (x != -28)
		return 0;

	if (x != -28 && x != -30) {
		__smatch_implied(x >> 1);
		__smatch_implied(1 >> x);
	}

	return 0;
}

/*
 * check-name: smatch: empty range right shift
 * check-command: smatch --disable=check_accidental_left_shift -I.. sm_empty_rshift.c
 *
 * check-output-start
sm_empty_rshift.c:9 test() implied: x >> 1 = 's32min-s32max'
sm_empty_rshift.c:10 test() implied: 1 >> x = 's32min-s32max'
 * check-output-end
 */
