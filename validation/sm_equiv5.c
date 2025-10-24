#include "check_debug.h"

int frob(void);

int wrap(int x)
{
	return x;
}

void function(void)
{
	int a, b;

	a = frob();
	b = wrap(a);
	if (b < 0 || b >= 10)
		return;
	__smatch_implied(a);
}

/*
 * check-name: smatch equiv #5
 * check-command: smatch -I.. sm_equiv5.c
 *
 * check-output-start
sm_equiv5.c:18 function() implied: a = '0-9'
 * check-output-end
 */
