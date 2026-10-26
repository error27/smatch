#include "extract-static-array.h"

static unsigned int count_values(void)
{
	return value_count();
}

/*
 * check-name: extract static array dependency
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-static-array.c.extract
 * check-output-end
 */
