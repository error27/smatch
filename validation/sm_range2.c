#include "check_debug.h"
int some_func();
int a, b, c, d, e;
int frob(void) {
	if (a)
		__smatch_value("a");
	else
		__smatch_value("a");
	__smatch_value("a");
	if (a) {
		b = 0;
		__smatch_value("b");
	}
	__smatch_value("b");
	c = 0;
	c = some_func();
	__smatch_value("c");
	if (d < -3 || d > 99)
		return;
	__smatch_value("d");
	if (d) {
		if (!e)
			return;
	}
	__smatch_value("d");
	__smatch_value("e");
}
/*
 * check-name: Smatch range test #2
 * check-command: smatch -I.. sm_range2.c
 *
 * check-output-start
sm_range2.c +6 frob(2) a = min-(-1),1-max
sm_range2.c +8 frob(4) a = 0
sm_range2.c +9 frob(5) a = min-max
sm_range2.c +12 frob(8) b = 0
sm_range2.c +14 frob(10) b = min-max
sm_range2.c +17 frob(13) c = unknown
sm_range2.c +20 frob(16) d = (-3)-99
sm_range2.c +25 frob(21) d = (-3)-99
sm_range2.c +26 frob(22) e = min-max
 * check-output-end
 */
