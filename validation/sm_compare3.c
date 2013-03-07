#include <stdio.h>
#include <string.h>
#include "check_debug.h"

int a, b, c, d;
int e, f, g;
int main(void)
{
	if (a >= b)
		return 1;
	if (a < 0 || b < 0)
		return 1;
	c = b - a;
	__smatch_implied(c);
	__smatch_compare(b, c);

	if (e < 0 || e > b)
		return;
	if (f <= 0 || f > b)
		return;
	g = e + f;

	__smatch_implied(g);
	__smatch_implied(e);
	__smatch_compare(g, e);
	__smatch_compare(e, g);
	__smatch_implied(g - e);
	__smatch_implied(g - f);

	return 0;
}


/*
 * check-name: Smatch compare #3
 * check-command: smatch -I.. sm_compare3.c
 *
 * check-output-start
sm_compare3.c:14 main() implied: c = '1-s32max'
sm_compare3.c:15 main() b <= c
sm_compare3.c:23 main() implied: g = '1-s32max'
sm_compare3.c:24 main() implied: e = '0-s32max'
sm_compare3.c:25 main() g > e
sm_compare3.c:26 main() e < g
sm_compare3.c:27 main() implied: g - e = '1-s32max'
sm_compare3.c:28 main() implied: g - f = '0-2147483646'
 * check-output-end
 */
