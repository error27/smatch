struct ture {
	int a;
};

struct ture *a;
struct ture *b;

void func (void)
{
	if (!a && !(a = returns_nonnull()))
		return;
	a->a = 1;

	if (b || (b = returns_nonnull())) {
		b->a  = 1;
		return;
	}
	b->a = 1;
}
/*
 * check-name: Compound Conditions
 * check-command: smatch sm_compound_condition.c
 *
 * check-output-start
sm_compound_condition.c +18 func(10) Dereferencing Undefined:  'b'
 * check-output-end
 */
