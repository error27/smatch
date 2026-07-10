#include "check_debug.h"

struct one {
	int x;
};

struct two {
	struct one *one;
};

struct aaa {
	struct two *two;
};

struct foo {
	int a, b, c;
	struct aaa *aaa;
};

void func(int x)
{
	struct foo foo;
	struct foo *p;
	struct aaa *a;

	p = &foo;
	a = p->aaa;

	__smatch_ssa(p);
	__smatch_ssa(a);

	__smatch_ssa(p->aaa);
	__smatch_ssa(p->a);
	__smatch_ssa(foo.a);
	__smatch_ssa(a->two);
}

/*
 * check-name: smatch: ssa ptr #1
 * check-command: smatch -I.. sm_ssa_ptr1.c
 *
 * check-output-start
sm_ssa_ptr1.c:29 func() name ssa_name: 'p => foo{2}'
sm_ssa_ptr1.c:30 func() name ssa_name: 'a => foo{2}->aaa'
sm_ssa_ptr1.c:32 func() name ssa_name: 'p->aaa => foo{2}->aaa'
sm_ssa_ptr1.c:33 func() name ssa_name: 'p->a => foo{2}->a'
sm_ssa_ptr1.c:34 func() name ssa_name: 'foo.a => foo{2}->a'
sm_ssa_ptr1.c:35 func() name ssa_name: 'a->two => foo{2}->aaa->two'
 * check-output-end
 */
