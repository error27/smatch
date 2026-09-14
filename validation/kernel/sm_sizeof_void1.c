#include <linux/compiler.h>
#include <linux/types.h>

bool variable_is_constant(unsigned int index);
bool variable_is_constant(unsigned int index)
{
	return __is_constexpr(index);
}

/*
 * check-name: smatch: no sizeof(void) from __is_constexpr()
 * check-command: validation/kernel/build.sh sm_sizeof_void1.c
 *
 * check-output-start
 * check-output-end
 */
