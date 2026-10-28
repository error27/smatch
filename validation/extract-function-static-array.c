static int values[] = { 1, 2 };

int target(void)
{
	return sizeof(values);
}

/*
 * check-name: extract function static array
 * check-command: extract --func=target $file && gcc -Werror -x c -fsyntax-only $file.extract.target
 *
 * check-output-start
extract: wrote extract-function-static-array.c.extract.target
 * check-output-end
 */
