#define __bitwise __attribute__((bitwise))

typedef unsigned long long __bitwise be64_t;

extern void consume(const be64_t *value);

int target(void)
{
	consume(0);
	return 0;
}

/*
 * check-name: extract typedef used through function pointer parameter
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-pointer-typedef.c.extract.target
 * check-output-end
 */
