#include "check_debug.h"

int *something();

int *one;
int *two;
int func(void)
{
	one = something();
	two = one;

	if (two == 1) {
		__smatch_implied(one);
		__smatch_implied(two);
	}
	__smatch_implied(one);
	__smatch_implied(two);
	if (one == 2) {
		__smatch_implied(one);
		__smatch_implied(two);
	}
	__smatch_implied(one);
	__smatch_implied(two);
	return 0;
}
/*
 * check-name: smatch equivalent variables #1
 * check-command: smatch -I.. -m64 sm_equiv1.c
 *
 * check-output-start
sm_equiv1.c:13 func() implied: one = '1'
sm_equiv1.c:14 func() implied: two = '1'
sm_equiv1.c:16 func() implied: one = '0-u64max'
sm_equiv1.c:17 func() implied: two = '0-u64max'
sm_equiv1.c:19 func() implied: one = '2'
sm_equiv1.c:20 func() implied: two = '2'
sm_equiv1.c:22 func() implied: one = '0-u64max'
sm_equiv1.c:23 func() implied: two = '0-u64max'
 * check-output-end
 */
