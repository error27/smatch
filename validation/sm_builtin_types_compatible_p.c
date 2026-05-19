#include "check_debug.h"

struct foo {
	int a;
};

struct bar {
	int a;
};

void frob(void)
{
	struct foo my_struct;
	int branch;

	if (__builtin_types_compatible_p(typeof(my_struct), struct foo))
		branch = 1;
	else if (__builtin_types_compatible_p(typeof(my_struct), struct bar))
		branch = 2;

	__smatch_implied(branch);
}


/*
 * check-name: smatch __builtin_types_compatible_p
 * check-command: smatch -I.. sm_builtin_types_compatible_p.c
 *
 * check-output-start
sm_builtin_types_compatible_p.c:21 frob() implied: branch = '1'
 * check-output-end
 */
