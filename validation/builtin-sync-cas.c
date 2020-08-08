static int *foo(int *ptr)
{
	__sync_val_compare_and_swap(ptr, 123, 0L);
	return __sync_val_compare_and_swap(&ptr, ptr, ptr);
}

static long bar(long *ptr)
{
	return __sync_val_compare_and_swap(ptr, ptr, 1);
}

static _Bool boz(_Bool *ptr)
{
	return __sync_bool_compare_and_swap(ptr, 0, ptr);
}

/*
 * check-name: builtin-sync-cas
 *
 * check-error-start
builtin-sync-cas.c:9:49: warning: incorrect type in argument 2 (different base types)
builtin-sync-cas.c:9:49:    expected long
builtin-sync-cas.c:9:49:    got long *ptr
 * check-error-end
 */
