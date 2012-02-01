#include "check_debug.h"
int a, b, c;
int frob(void) {
	if (a && b != 1)
		return;

	__smatch_value("a");
	if (b == 0 && c) {
		__smatch_value("a");
	}
	__smatch_value("a");
}
/*
 * check-name: Smatch implied #7
 * check-command: smatch -I.. sm_implied7.c
 *
 * check-output-start
sm_implied7.c:7 frob(4) a = min-max
sm_implied7.c:9 frob(6) a = 0
sm_implied7.c:11 frob(8) a = min-max
 * check-output-end
 */
