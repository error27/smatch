#include "check_debug.h"

int a, b, c, d;
static int options_write(void)
{
	a = d;
	if (a > b + c)
		a = b + c;
	__smatch_compare(a, d);
}

/*
 * check-name: smatch compare #11
 * check-command: smatch -I.. sm_compare11.c
 *
 * check-output-start
sm_compare11.c:9 options_write() a <= d
 * check-output-end
 */
