int target(const void *ptr)
{
	const union {
		int value;
	} *item = ptr;

	return item->value;
}

/*
 * check-name: extract const anonymous union
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-const-anonymous-union.c.extract
 * check-output-end
 */
