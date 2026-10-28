#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise flag_t;

static inline flag_t increment(flag_t value)
{
	return value + 1;
}

int target(void)
{
	return increment(0);
}

/*
 * check-name: extract typedef used by inline function parameter
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-inline-typedef-parameter.c.extract.last
 * check-output-end
 */
