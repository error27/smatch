#include "check_debug.h"

int *something();

int red;
int blue;
int x;
int func(void)
{
	red = 0;

	if (x) {
		red = 5;
	}
	blue = red;

	if (x) {
		__smatch_value("red");
		/* __smatch_value("blue"); this doesn't work */
	}
	__smatch_value("red");
	__smatch_value("blue");
	return 0;
}
/*
 * check-name: smatch equivalent variables #2 (implications)
 * check-command: smatch -I.. sm_equiv2.c
 *
 * check-output-start
sm_equiv2.c +18 func(10) red = 5
sm_equiv2.c +21 func(13) red = 0,5
sm_equiv2.c +22 func(14) blue = 0,5
 * check-output-end
 */
