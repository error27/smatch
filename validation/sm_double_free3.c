#include "check_debug.h"

void kfree(void *p) {}
void *frob(void);

struct foo {
	void *p;
};

void func(struct foo *p)
{
	kfree(&p->p);
	p = frob();
	kfree(&p->p);
	kfree(&p->p);
}

/*
 * check-name: smatch: double free #3
 * check-command: validation/smatch_db_test.sh -p=kernel -I.. sm_double_free3.c
 *
 * check-output-start
sm_double_free3.c:15 func() error: double free of '&p->p' (line 14)
 * check-output-end
 */
