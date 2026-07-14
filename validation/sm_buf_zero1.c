#include <string.h>
#include "check_debug.h"

struct foo {
	int a, b, c;
};

int main(int x)
{
	struct foo foo;
	struct foo *p;

	memset(&foo, 0, sizeof(foo));
	p = &foo;

	__smatch_implied(p->a);
	__smatch_implied(foo.a);

	return 0;
}


/*
 * check-name: smatch: buf zero #1
 * check-command: smatch -I.. sm_buf_zero1.c
 *
 * check-output-start
sm_buf_zero1.c:16 main() implied: p->a = '0'
sm_buf_zero1.c:17 main() implied: foo.a = '0'
 * check-output-end
 */
