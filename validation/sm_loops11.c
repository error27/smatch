#include "check_debug.h"
#include "smatch.h"

int frob(void);

void func(int a, int b, int c)
{
	while (a++ < 100) {
		__smatch_implied(a);
		if (frob())
			break;
	}
	__smatch_implied(a);

	if (b < 10 || b > 200)
		return;

	while (b++ < 100) {
		__smatch_implied(b);
		if (frob())
			break;
	}
	__smatch_implied(b);

	if (c < 0 || c > 50)
		return;

	while (c++ <= 100) {
		__smatch_implied(c);
	}
	__smatch_implied(c);
}

/*
 * check-name: smatch: loops handling #11
 * check-command: smatch -I.. sm_loops11.c
 *
 * check-output-start
sm_loops11.c:9 func() implied: a = 's32min-100'
sm_loops11.c:13 func() implied: a = 's32min-s32max'
sm_loops11.c:19 func() implied: b = '11-100'
sm_loops11.c:23 func() implied: b = '11-201'
sm_loops11.c:29 func() implied: c = '1-101'
sm_loops11.c:31 func() implied: c = '102'
 * check-output-end
 */
