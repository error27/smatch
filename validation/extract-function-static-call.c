static int helper(void)
{
	return 1;
}

int target(void)
{
	return helper();
}

/*
 * check-name: extract function static call
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-static-call.c.extract.target
 * check-output-end
 */
