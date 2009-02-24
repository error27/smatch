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
