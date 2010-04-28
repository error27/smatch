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
	e->a = 1;
	d = returns_nonnull();
	if (c?d:e) {
		c->a = 1;
		d->a = 1;
		e->a = 1;
	}
	e->a = 1;
}

/*
 * check-name: Ternary Conditions
 * check-command: smatch sm_select.c
 *
 * check-output-start
sm_select.c +17 func(6) error: we previously assumed 'a' could be null.
sm_select.c +18 func(7) error: we previously assumed 'b' could be null.
sm_select.c +21 func(10) warn: variable dereferenced before check 'e'
sm_select.c +22 func(11) error: we previously assumed 'c' could be null.
sm_select.c +26 func(15) error: we previously assumed 'e' could be null.
 * check-output-end
 */
