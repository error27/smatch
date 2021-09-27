void *alloc(unsigned long)__attribute__((alloc_size(1)));

_Bool sta(void)
{
	void *ptr = alloc(4);
	return __builtin_object_size(ptr, 0) == 4;
}

_Bool dyn(unsigned long n)
{
	void *ptr = alloc(n);
	return __builtin_object_size(ptr, 0) == n;
}

/*
 * check-name: builtin-objsize-dyn
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-returns: 1
 */
