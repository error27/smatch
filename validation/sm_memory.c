void *kmalloc(void);

struct ture {
	int *a;
};

struct ture *a;
struct ture *b;
void func (void)
{
	struct ture *aa;
	struct ture *ab;
	struct ture *ac;
	aa = kmalloc();
	ab = kmalloc();
	ac = kmalloc();

	a = aa;
	if (ab) {
		free(ab);
		return;
	}
	free(ac);
	return;
}
/*
 * check-name: leak test #1
 * check-command: smatch sm_memory.c
 *
 * check-output-start
sm_memory.c:21 func(12) warn: possible memory leak of 'ac'
 * check-output-end
 */
