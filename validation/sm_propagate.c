#include "check_debug.h"

int frob();

int func(int *p)
{
	int ret;

	ret = frob();
	if (ret < 0)
		return -1;
	return 0;
}
/*
 * check-name: Smatch propagate return codes
 * check-command: smatch -p=kernel -I.. sm_propagate.c
 *
 * check-output-start
sm_propagate.c:11 func() info: why not propagate 'ret' from frob() instead of (-1)?
 * check-output-end
 */
