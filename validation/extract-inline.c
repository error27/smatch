#include "extract-inline.h"

static int call_f(int value)
{
	return f(value) + g(value);
}

/*
 * check-name: extract inline definition
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-inline.c.extract
 * check-output-end
 */
