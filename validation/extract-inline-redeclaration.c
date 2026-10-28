static inline int helper(void)
{
	return 1;
}

static inline int helper(void);

int target(void)
{
	return helper();
}

/*
 * check-name: extract definition before inline redeclaration
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-inline-redeclaration.c.extract.last
 * check-output-end
 */
