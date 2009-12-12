struct ture {
	int a;
};
struct cont {
	struct ture *x;
};

struct ture *x;
struct ture **px;
struct cont *y;
void func (void)
{
	int *a = &(x->a);
	int *b = &x->a;
	int *c = &(y->x->a);
	int *d = &((*px)->a);

	if (x)
		;
	if (px)
		;
	if (y->x)
		;
	if (y)
		;

	return;
}
/*
 * check-name: Dereferencing before check
 * check-command: smatch sm_deref_check_deref.c
 *
 * check-output-start
sm_deref_check_deref.c +20 func(9) warn: variable dereferenced before check 'px'
sm_deref_check_deref.c +24 func(13) warn: variable dereferenced before check 'y'
 * check-output-end
 */
