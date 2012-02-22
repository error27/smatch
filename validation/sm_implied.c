struct ture {
	int a;
};

struct ture *a;
struct ture *b;

void func (void)
{
	struct ture *aa;

	b = 0;
	if (a)
		goto x;
	aa = returns_nonnull();
	b = 1;
x:
	if (b)
		aa->a = 1;
	aa->a = 1;
	return;
}
/*
 * check-name: Smatch implied #1
 * check-command: smatch sm_implied.c
 *
 * check-output-start
sm_implied.c:20 func() error: potentially derefencing uninitialized 'aa'.
 * check-output-end
 */
