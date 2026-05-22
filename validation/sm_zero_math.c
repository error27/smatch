#include "check_debug.h"

int frob(int x, int y)
{
	if (x < -10 || x > 10)
		return 0;

	__smatch_implied(x);
	__smatch_implied(0 % x);
	__smatch_implied(0 / x);
	__smatch_implied(0 * x);
	__smatch_implied(x * 0);
	__smatch_implied(x + 0);
	__smatch_implied(x - 0);
	__smatch_implied(x << 0);
	__smatch_implied(x >> 0);
	__smatch_implied(0 & x);
	__smatch_implied(x & 0);
	__smatch_implied(x ^ 0);
	__smatch_implied(0 ^ x);
	__smatch_implied(0 | x);
	__smatch_implied(x | 0);

	return 0;
}


/*
 * check-name: plus minus zero
 * check-command: ./smatch --disable=check_OR_vs_AND -I.. sm_zero_math.c
 *
 * check-output-start
sm_zero_math.c:8 frob() implied: x = '(-10)-10'
sm_zero_math.c:9 frob() implied: 0 % x = '0'
sm_zero_math.c:10 frob() implied: 0 / x = '0'
sm_zero_math.c:11 frob() implied: 0 * x = '0'
sm_zero_math.c:12 frob() implied: x * 0 = '0'
sm_zero_math.c:13 frob() implied: x + 0 = '(-10)-10'
sm_zero_math.c:14 frob() implied: x - 0 = '(-10)-10'
sm_zero_math.c:15 frob() implied: x << 0 = '(-10)-10'
sm_zero_math.c:16 frob() implied: x >> 0 = '(-10)-10'
sm_zero_math.c:17 frob() implied: 0 & x = '0'
sm_zero_math.c:18 frob() implied: x & 0 = '0'
sm_zero_math.c:19 frob() implied: x ^ 0 = '(-10)-10'
sm_zero_math.c:20 frob() implied: 0 ^ x = '(-10)-10'
sm_zero_math.c:21 frob() implied: 0 | x = '(-10)-10'
sm_zero_math.c:22 frob() implied: x | 0 = '(-10)-10'
 * check-output-end
 */
