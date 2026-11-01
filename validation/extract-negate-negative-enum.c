enum { ERR = -4 };

int test(int index)
{
	long l = index;

	if (index > -ERR - 1)
		return 1;
	return l > -ERR;
}

/*
 * check-name: extract negation of a negative enum value
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-negate-negative-enum.c.extract.last
 * check-output-end
 */
