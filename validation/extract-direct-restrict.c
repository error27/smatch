int target(int *restrict value)
{
	return *value;
}

/*
 * check-name: extract direct restrict
 * check-command: extract $file && grep -Fqx 'extern int target(int *restrict value)' $file.extract && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-direct-restrict.c.extract
 * check-output-end
 */
