/*
 * check-name: extract complete type from asm sizeof operand
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-asm-sizeof.c.extract
 * check-output-end
 */

#include "extract-asm-sizeof.h"

void use_asm_sizeof(void)
{
	asm volatile ("" : : [size] "i" (sizeof(struct asm_sizeof_type)));
}
