#include "check_debug.h"

struct my_struct {
	int a, b, c;
};

void *alloc_p(void);

static void frob(int *x, struct my_struct *p)
{
	*x = p->a;
	p->a = 0;
}

void func(void)
{
	struct my_struct *p;
	int b;

	p = alloc_p();
	if (p->a < 0 || p->a > 10)
		return;

	frob(&b, p);
	__smatch_implied(p->a);
	__smatch_implied(b);
}

/*
 * check-name: Smatch: param key 2
 * check-command: smatch -I.. sm_param_key2.c
 *
 * check-output-start
sm_param_key2.c:25 func() implied: p->a = '0'
sm_param_key2.c:26 func() implied: b = '0-10'
 * check-output-end
 */
