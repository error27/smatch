/*
 * check-name: extract initializer inline dependency
 * check-command: extract $file && grep -Fq 'static inline int header_callback(void)' $file.extract && grep -Fq 'return 1;' $file.extract && gcc -Werror -Wall -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-initializer-inline.c.extract
 * check-output-end
 */

#include "extract-initializer-inline.h"

static int (*callback)(void) = header_callback;
