//#include <stdlib.h>

struct foo {
	int a;
};

struct foo a;
struct foo b;
struct foo c;
struct foo d;

static void func (void)
{
	struct foo *aa;
	int ab = 0;
	int ac = 1;

	aa->a = 1;

	if (a) {
		a->a = 1;
	}
	a->a = 1;

	if (a && b) {
		b->a = 1;
	}

	if (a || b) {
		b->a = 1;
	}

	if (c) {
		ab = 1;
	}

	if (ab) {
		c->a = 1;
	}

	d = 0;
	if (c) {
		d = malloc(sizeof(*d));
		ac = 2;
	}

	if (ac) {
		d->a = 2;
	}
}
/*
 * check-name: Null Dereferences
 * check-command: smatch --spammy sm_null_deref.c
 *
 * check-output-start
sm_null_deref.c +18 func(6) error: dereferencing undefined:  'aa'
sm_null_deref.c +18 func(6) error: potentially derefencing uninitialized 'aa'.
sm_null_deref.c +23 func(11) error: dereferencing undefined:  'a'
sm_null_deref.c +25 func(13) warn: variable dereferenced before check 'a'
sm_null_deref.c +30 func(18) error: dereferencing undefined:  'b'
sm_null_deref.c +48 func(36) error: dereferencing undefined:  'd'
sm_null_deref.c +48 func(36) error: potential null derefence 'd'.
 * check-output-end
 */
