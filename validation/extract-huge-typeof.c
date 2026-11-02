#define A(x) ((x) + (x) + (x) + (x))
#define HUGE(x) A(A(A(A(A(A(x))))))

int value;

int test(void)
{
	typeof(HUGE(value)) x = 0;

	return x;
}

/*
 * check-name: extract a typeof() with an expression over 16k
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-huge-typeof.c.extract.test
 * check-output-end
 */
