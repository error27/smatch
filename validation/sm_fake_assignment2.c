#include "check_debug.h"

int frob(int *yyy, int q)
{
	*yyy = q;
	return q;
}

int func(int *p)
{
	int i;
	int a, xxx, c;

	c = 1;
	for (i = 0; i < 10; i++) {
		a = frob(&xxx, c) * 8;
		__smatch_implied(xxx);
		__smatch_implied(c);
		c = 2;
	}

	return 0;
}

/*
 * check-name: smatch: fake assignment #2
 * check-command: ./smatch -I.. sm_fake_assignment2.c
 *
 * check-output-start
sm_fake_assignment2.c:17 func() implied: xxx = '1-2'
sm_fake_assignment2.c:18 func() implied: c = '1-2'
 * check-output-end
 */
