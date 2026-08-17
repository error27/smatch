void *kmalloc(unsigned long size, int flags);

void *alloc(void)
{
	void *p;

	p = kmalloc(16, 0);
	return p;
}

void *not_alloc(void *p)
{
	return p;
}

/*
 * check-name: smatch: check allocation functions
 * check-command: smatch --no-data --info -p=kernel sm_check_allocation_funcs.c | grep 'info: allocation func'
 *
 * check-output-start
sm_check_allocation_funcs.c:8 alloc() info: allocation func
 * check-output-end
 */
