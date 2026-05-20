#include "check_debug.h"

static void *ERR_PTR(int err)
{
	return (void *)(unsigned long)err;
}

void func(unsigned long a)
{
	void *p;
	if (a <= 0 || a > 4095)
		return;
	__smatch_implied(-a);
	p = ERR_PTR(-a);
}

/*
 * check-name: smatch: compare #19
 * check-command: ./smatch -p=kernel -I.. sm_compare19.c
 *
 * check-output-start
sm_compare19.c:13 func() implied: -a = '18446744073709547521-u64max'
 * check-output-end
 */
