#include <string.h>
#include "check_debug.h"

struct foo {
	int a, b, c;
};

int frob(void);

void func(struct foo *p, struct foo *q)
{
	struct foo foo;

	if (frob())
		p = &foo;
	else
		p = NULL;

	q = frob() ? &foo : NULL;

	__smatch_ssa_pointer(&foo);

	memset(&foo, 0, sizeof(foo));

	__smatch_ssa(foo.a);
	__smatch_ssa(p->a);
	__smatch_ssa(p);

	__smatch_implied(foo.a);
	__smatch_implied(p->a);
	__smatch_implied(q->a);
}

/*
 * check-name: smatch: buf zero #1
 * check-command: smatch -I.. sm_buf_zero1.c
 *
 * check-output-start
sm_buf_zero1.c:25 func() name ssa_name: 'foo.a => &foo{0}->a'
sm_buf_zero1.c:26 func() name ssa_name: 'p->a => &foo{0}->a'
sm_buf_zero1.c:27 func() name ssa_name: 'p => &foo{0}'
sm_buf_zero1.c:29 func() implied: foo.a = '0'
sm_buf_zero1.c:30 func() implied: p->a = '0'
sm_buf_zero1.c:31 func() implied: q->a = '0'
 * check-output-end
 */
