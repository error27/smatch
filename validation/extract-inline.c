#include "extract-inline.h"

static int call_f(int value)
{
	return f(value);
}

/*
 * check-name: extract inline definition
 * check-command: extract $file&& PATH=.. extract $file.extract && grep -q 0x1 $file.extract.extract
 *
 * check-output-start
extract: wrote extract-inline.c.extract
extract: wrote extract-inline.c.extract.extract
 * check-output-end
 */
