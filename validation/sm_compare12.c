#include "check_debug.h"

#define min_t(type, x, y) ({			\
	type __min1 = (x);			\
	type __min2 = (y);			\
	__min1 < __min2 ? __min1: __min2; })

int a, b, c, d;
static int options_write(void)
{
	a = min_t(int, b + c, d);
	__smatch_compare(a, d);
	__smatch_compare(a, b + c);
}

/*
 * check-name: smatch compare #12
 * check-command: smatch -I.. sm_compare12.c
 *
 * check-output-start
sm_compare12.c:12 options_write() a <= d
sm_compare12.c:13 options_write() a <= b + c
 * check-output-end
 */
