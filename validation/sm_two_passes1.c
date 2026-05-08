#include "check_debug.h"

int check(int x)
{
	int ret = 0;

	if (x < 0 || x > 10)
		goto invalid;
out:
	return ret;
invalid:
	ret = -1;
	goto out;
}

void func(int a)
{
	int ret;

	ret = check(a);
	if (ret)
		return ret;
	__smatch_implied(a);
}

/*
 * check-name: smatch: two passes #1
 * check-command: smatch -I.. sm_two_passes1.c
 *
 * check-output-start
sm_two_passes1.c:23 func() implied: a = '0-10'
 * check-output-end
 */
