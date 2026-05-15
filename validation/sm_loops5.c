#include "check_debug.h"

int frob(void);

int a, b, c;
void test(void)
{
	a = 0;
	do {
		__smatch_implied(a);
	} while (a++ < 3);
	__smatch_implied(a);
}
/*
 * check-name: smatch loops #5
 * check-command: smatch -I.. sm_loops5.c
 *
 * check-output-start
sm_loops5.c:10 test() implied: a = '0-3'
sm_loops5.c:12 test() implied: a = '4'
 * check-output-end
 */
