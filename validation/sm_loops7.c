#include "check_debug.h"

int frob(void);

int a, b, c;
void test(void)
{
	a = 0;
	do {
		__smatch_implied(a);
		a++;
		__smatch_implied(a);
	} while (0);
	__smatch_implied(a);
}

/*
 * check-name: smatch loops #7
 * check-command: smatch -I.. sm_loops7.c
 *
 * check-output-start
sm_loops7.c:10 test() implied: a = '0'
sm_loops7.c:12 test() implied: a = '1'
sm_loops7.c:14 test() implied: a = '1'
 * check-output-end
 */
