#include "check_debug.h"

void func(void)
{
	int a;
	int retry = 0;

	a = 0;
	while (retry++ < 2) {
		a++;
		__smatch_implied(retry);
		__smatch_compare(retry, 2);
	}
	__smatch_implied(a);
	__smatch_implied(retry);
}

/*
 * check-name: Smatch: loops handling #10
 * check-command: smatch -I.. sm_loops10.c
 *
 * check-output-start
sm_loops10.c:11 func() implied: retry = '1-2'
sm_loops10.c:12 func() retry <= 2
sm_loops10.c:14 func() implied: a = '1-s32max'
sm_loops10.c:15 func() implied: retry = '3'
 * check-output-end
 */
