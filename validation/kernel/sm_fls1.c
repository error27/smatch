#include <linux/slab.h>

#include "../../check_debug.h"

struct big {
	char data[512];
};

void test(void);
void test(void)
{
	struct big *p;

	p = kvzalloc_obj(*p);
	__smatch_states("impossible");
	kvfree(p);
}

/*
 * check-name: smatch: fls() empties estate #1
 * check-command: validation/kernel/build.sh sm_fls1.c
 *
 * check-output-start
sm_fls1.c:15 test() no states found for 'impossible'
sm_fls1.c:15 test() impossible: no states
 * check-output-end
 */
