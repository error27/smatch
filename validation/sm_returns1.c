#include "check_debug.h"

struct my_struct {
	int a, b, c;
};

struct my_struct *alloc_p(void);

static int frob(struct my_struct *p)
{
	int ret = p->a;
	p->a = 0;
	__smatch_return_str(ret);
	return ret;
}

void func(void)
{
	struct my_struct *p;
	int b;

	p = alloc_p();
	if (p->a < 0 || p->a > 10)
		return;

	b = frob(p);
	__smatch_implied(p->a);
	__smatch_implied(b);
}

/*
 * check-name: Smatch: return handling
 * check-command: smatch -I.. sm_returns1.c
 *
 * check-output-start
sm_returns1.c:13 frob() ret_str='s32min-s32max[$0->a]'
sm_returns1.c:27 func() implied: p->a = '0'
sm_returns1.c:28 func() implied: b = '0-10'
 * check-output-end
 */
