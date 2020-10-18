void foo(void *ptr, _Bool *bptr, volatile void *vptr, volatile _Bool *vbptr, int mo)
{
	__atomic_clear(ptr, mo);
	__atomic_clear(bptr, mo);
	__atomic_clear(vptr, mo);
	__atomic_clear(vbptr, mo);
}

/*
 * check-name: builtin-atomic-clear
 *
 * check-error-start
builtin-atomic-clear.c:1:6: warning: symbol 'foo' was not declared. Should it be static?
 * check-error-end
 */
