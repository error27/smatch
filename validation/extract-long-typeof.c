#define A(x) ((x) + (x) + (x) + (x))
#define LONG(x) A(A(A(A(A(x)))))

int value;

int test(void)
{
	typeof(LONG(value)) x = 0;

	return x;
}

/*
 * check-name: extract a typeof() with a long expression
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-long-typeof.c.extract.test
 * check-output-end
 */
