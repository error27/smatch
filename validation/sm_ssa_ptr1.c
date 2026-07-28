#include "check_debug.h"

struct one {
	int x;
};

struct two {
	struct one *one;
};

struct aaa {
	struct one one;
	struct two *two;
};

struct foo {
	int a, b, c;
	struct aaa *aaa;
};

int main(int x)
{
	struct foo foo;
	struct foo *p;
	struct aaa *a;
	struct one *one;

	p = &foo;
	a = p->aaa;
	one = &foo.aaa->one;

	__smatch_ssa(p);
	__smatch_ssa(a);

	__smatch_ssa(p->aaa);
	__smatch_ssa(p->a);
	__smatch_ssa(foo.a);
	__smatch_ssa(a->two);
	__smatch_ssa(one);
	__smatch_ssa(one->x);

	return 0;
}


/*
 * check-name: smatch: ssa ptr #1
 * check-command: smatch -I.. sm_ssa_ptr1.c
 *
 * check-output-start
sm_ssa_ptr1.c:32 main() name ssa_name: 'p => &foo{0}'
sm_ssa_ptr1.c:33 main() name ssa_name: 'a => (&foo{0})->aaa'
sm_ssa_ptr1.c:35 main() name ssa_name: 'p->aaa => (&foo{0})->aaa'
sm_ssa_ptr1.c:36 main() name ssa_name: 'p->a => (&foo{0})->a'
sm_ssa_ptr1.c:37 main() name ssa_name: 'foo.a => (&foo{0})->a'
sm_ssa_ptr1.c:38 main() name ssa_name: 'a->two => (&foo{0})->aaa->two'
sm_ssa_ptr1.c:39 main() name ssa_name: 'one => (&foo{0})->aaa->one'
sm_ssa_ptr1.c:40 main() name ssa_name: 'one->x => (&foo{0})->aaa->one->x'
 * check-output-end
 */
