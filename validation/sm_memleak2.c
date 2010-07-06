void func (void)
{
	void *ptr;

	ptr = malloc(42);
	ptr = (void *) 0;

	return;
}
/*
 * check-name: leak test #2
 * check-command: smatch sm_memleak2.c
 *
 * check-output-start
sm_memleak2.c +6 func(5) error: memory leak of 'ptr'
 * check-output-end
 */
