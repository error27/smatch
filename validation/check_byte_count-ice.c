extern void *memset (void *s, int c, int n);

static void foo(void *a)
{
	memset(foo, + ', 20);
}
/*
 * check-name: Segfault in check_byte_count after syntax error
 *
 * check-error-start
check_byte_count-ice.c:5:18: error: Bad character constant
check_byte_count-ice.c:5:8: error: not enough arguments for function memset
 * check-error-end
 */
