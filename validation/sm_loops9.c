#include "check_debug.h"

void test(signed char a, unsigned char b, int c, unsigned int d, long e, unsigned long f)
{
	int i;

	if (f == 3)
		;

	for (i = 0; i < a; i++)
		__smatch_implied(i);
	for (i = 0; i < b; i++)
		__smatch_implied(i);
	for (i = 0; i < c; i++)
		__smatch_implied(i);
	for (i = 0; i < d; i++)
		__smatch_implied(i);
	for (i = 0; i < e; i++)
		__smatch_implied(i);
	for (i = 0; i < f; i++)
		__smatch_implied(i);
}

/*
 * check-name: smatch: loops #9
 * check-command: ./smatch -I.. sm_loops9.c
 *
 * check-output-start
sm_loops9.c:11 test() implied: i = '0-126'
sm_loops9.c:13 test() implied: i = '0-254'
sm_loops9.c:15 test() implied: i = '0-s32max'
sm_loops9.c:17 test() implied: i = '0-s32max'
sm_loops9.c:19 test() implied: i = '0-s32max'
sm_loops9.c:21 test() implied: i = '0-s32max'
 * check-output-end
 */
