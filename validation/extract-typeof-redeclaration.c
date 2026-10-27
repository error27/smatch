/*
 * check-name: extract typeof redeclaration
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-redeclaration.c.extract
 * check-output-end
 */

#include "extract-typeof-redeclaration.h"

extern typeof(target) target;

static int call_target(void)
{
	return target(0);
}
