#include <string.h>
#include "check_debug.h"

int frob(void);

struct foo {
	int a, b, c;
};

void func(struct foo *p)
{
	struct foo foo;

	if (frob())
		memset(p, 0, sizeof(*p));
	memset(&foo, 0, sizeof(foo));

	__smatch_is_clear(foo.a);
	__smatch_is_clear(p->b);
	memset(p, 0, sizeof(*p));
	__smatch_is_clear(p->b);
}

/*
 * check-name: smatch: cleared #1
 * check-command: smatch -I.. sm_buf_cleared1.c
 *
 * check-output-start
sm_buf_cleared1.c:18 func() foo.a: zeroed
sm_buf_cleared1.c:19 func() p->b: cleared
sm_buf_cleared1.c:21 func() p->b: zeroed
 * check-output-end
 */
