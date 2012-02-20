#include "check_debug.h"

int a, b, c;

static int frob(void)
{
	if (a > 5)
		return;
	if (b > 5)
		return;
	if (c != 5)
		return;

	if (a == 10)
		__smatch_value("a");
	if (b != 10)
		__smatch_value("b");
	if (c != 5)
		__smatch_value("c");
	if (5 != c)
		__smatch_value("c");

	__smatch_value("a");
	__smatch_value("b");
	__smatch_value("c");
}

/*
 * check-name: Smatch Comparison
 * check-command: smatch -I.. sm_compare.c
 *
 * check-output-start
sm_compare.c:15 frob(10) a = empty
sm_compare.c:17 frob(12) b = min-5
sm_compare.c:19 frob(14) c = empty
sm_compare.c:21 frob(16) c = empty
sm_compare.c:23 frob(18) a = min-5
sm_compare.c:24 frob(19) b = min-5
sm_compare.c:25 frob(20) c = 5
 * check-output-end
 */
