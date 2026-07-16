#include <string.h>
#include <stdlib.h>
#include "check_debug.h"

struct foo {
	int a, b, c;
};

void one(struct foo *p)
{
	__smatch_implied(p->a);
}

void two(struct foo *p)
{
	__smatch_implied(p->b);
}

void three(struct foo *p)
{
	__smatch_implied(p->c);
}

int func(void)
{
	struct foo *p, *q;

	p = calloc(1, sizeof(*p));
	if (!p)
		return -1;

	q = malloc(sizeof(*p));
	if (!q)
		return -1;

	one(p);

	two(q);

	three(p);
	three(q);

	return 0;
}

/*
 * check-name: smatch: buf zero #2
 * check-command: validation/smatch_db_test.sh -I.. sm_buf_zero2.c
 *
 * check-output-start
sm_buf_zero2.c:11 one() implied: p->a = '0'
sm_buf_zero2.c:16 two() implied: p->b = 's32min-s32max'
sm_buf_zero2.c:21 three() implied: p->c = 's32min-s32max'
 * check-output-end
 */
