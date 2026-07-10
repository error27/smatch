#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "../../check_debug.h"

struct event_filter {
	int a, b, c;
};

int test_alloc2(void __user *argp);
int test_alloc2(void __user *argp)
{
	struct event_filter *p;

	p = kvzalloc_obj(*p);
	if (!p)
		return -ENOMEM;

	__smatch_implied(p->a);

	return 0;
}


/*
 * check-name: smatch: kzalloc #2
 * check-command: validation/kernel/build.sh sm_kzalloc2.c
 *
 * check-output-start
sm_kzalloc2.c:21 test_alloc2() implied: p->a = '0'
 * check-output-end
 */
