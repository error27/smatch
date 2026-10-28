static int second(void);

static int first(void)
{
	return second();
}

static int second(void)
{
	return first();
}

int target(void)
{
	return first();
}

/*
 * check-name: extract mutually recursive static functions
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-static-function-cycle.c.extract.last
 * check-output-end
 */
