struct foo {
	int a;
};

struct foo *a;
struct foo *b;

struct foo *c;
struct foo *d;
struct foo *e;
void func (void)
{
	if (a?b:0) {
		a->a = 1;
		b->a = 1;
	}
	a->a = 1;
	b->a = 1;
	d = returns_nonnull();
	if (c?d:e) {
		c->a = 1;
		d->a = 1;
		e->a = 1;
	}
}

/*
 * check-name: Ternary Conditions
 * check-command: smatch --spammy sm_select.c
 *
 * check-output-start
sm_select.c +17 func(6) error: dereferencing undefined:  'a'
sm_select.c +18 func(7) error: dereferencing undefined:  'b'
sm_select.c +21 func(10) error: dereferencing undefined:  'c'
sm_select.c +23 func(12) error: dereferencing undefined:  'e'
 * check-output-end
 */
