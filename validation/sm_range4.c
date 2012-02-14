#include "check_debug.h"

int a, b, c;

static int frob(void)
{
	if (a > 5) {
		__smatch_value("a");
		return;
	}
	if (b++ > 5) {
		__smatch_value("b");
		return;
	}
	if (++c > 5) {
		__smatch_value("c");
		return;
	}
	__smatch_value("a");
	__smatch_value("b");
	__smatch_value("c");
}


/*
 * check-name: Smatch Range #4
 * check-command: smatch -I.. sm_range4.c
 *
 * check-output-start
sm_range4.c:8 frob(3) a = 6-max
sm_range4.c:12 frob(7) b = 7-max
sm_range4.c:16 frob(11) c = 6-max
sm_range4.c:19 frob(14) a = min-5
sm_range4.c:20 frob(15) b = min-6
sm_range4.c:21 frob(16) c = min-5
 * check-output-end
 */
