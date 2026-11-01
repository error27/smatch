#ifndef INCLUDED
#define INCLUDED
typedef unsigned long long u64;

static void copy(void *dst, void *src)
{
	u64 v = ({
		u64 val;
		__builtin_memcpy(&val, src, sizeof(val));
		val;
	});
	__builtin_memcpy(dst, &v, sizeof(v));
}
#else
void test(void *dst, void *src)
{
	copy(dst, src);
}
#endif

/*
 * check-name: extract a function from an -include file
 * check-command: extract --last-func -include $file $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-include-function.c.extract.last
 * check-output-end
 */
