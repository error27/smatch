#include "check_debug.h"

int frob(int a, int b, int c, int d, int e)
{
	if (a < 0 || a > 3)
		return 0;
	if (b >= a)
		return 0;
	if (c > a)
		return 0;

	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(c);
	__smatch_compare(a, b);
	__smatch_implied(a - b);
	__smatch_compare(a, c);
	__smatch_implied(a - c);

	if (a == 0)
		return;
	if (d >= a)
		return 0;
	if (e > a)
		return 0;

	__smatch_implied(a);
	__smatch_implied(a - d);
	__smatch_implied(a - e);

	return 0;
}

/*
 * check-name: smatch: subtract #3
 * check-command: ./smatch -I.. sm_subtract3.c
 *
 * check-output-start
sm_subtract3.c:12 frob() implied: a = '0-3'
sm_subtract3.c:13 frob() implied: b = 's32min-2'
sm_subtract3.c:14 frob() implied: c = 's32min-3'
sm_subtract3.c:15 frob() a > b
sm_subtract3.c:16 frob() implied: a - b = '1-s32max'
sm_subtract3.c:17 frob() a >= c
sm_subtract3.c:18 frob() implied: a - c = '0-s32max'
sm_subtract3.c:27 frob() implied: a = '1-3'
sm_subtract3.c:28 frob() implied: a - d = '1-s32max'
sm_subtract3.c:29 frob() implied: a - e = '0-s32max'
 * check-output-end
 */
