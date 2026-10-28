static int first(void)
{
	return 1;
}

int last(void)
{
	return first();
}

/*
 * check-name: extract last function
 * check-command: extract --last-func $file && test -f $file.extract.last && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-last-func.c.extract.last
 * check-output-end
 */
