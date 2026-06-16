#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "../../check_debug.h"

int test_alloc2(void __user *argp);
int test_alloc2(void __user *argp)
{
	char *a, *b, *c;

	a = kmalloc(100, GFP_KERNEL);
	b = kzalloc(100, GFP_KERNEL);
	c = kmalloc_array(100, 1, GFP_KERNEL);
	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(c);
	kfree(a);
	kfree(b);
	kfree(c);

	a = kvmalloc(100, GFP_KERNEL);
	b = kvzalloc(100, GFP_KERNEL);
	c = kvmalloc_array(100, 1, GFP_KERNEL);
	__smatch_implied(a);
	__smatch_implied(b);
	__smatch_implied(c);
	kfree(a);
	kfree(b);
	kfree(c);

	a = kmalloc(0, GFP_KERNEL);
	__smatch_implied(a);

	return 0;
}


/*
 * check-name: smatch: kmalloc #3
 * check-command: validation/kernel/build.sh sm_kmalloc3.c
 *
 * check-output-start
sm_kmalloc3.c:16 test_alloc2() implied: a = '0,4096-ptr_max'
sm_kmalloc3.c:17 test_alloc2() implied: b = '0,4096-ptr_max'
sm_kmalloc3.c:18 test_alloc2() implied: c = '0,4096-ptr_max'
sm_kmalloc3.c:26 test_alloc2() implied: a = '0,4096-ptr_max'
sm_kmalloc3.c:27 test_alloc2() implied: b = '0,4096-ptr_max'
sm_kmalloc3.c:28 test_alloc2() implied: c = '0,4096-ptr_max'
sm_kmalloc3.c:34 test_alloc2() implied: a = '16'
 * check-output-end
 */
