#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "../../check_debug.h"

struct event_filter {
	int a, b, c;
	__u32 nevents;
	__DECLARE_FLEX_ARRAY(__u64, events);
};

int test_alloc2(void __user *argp);
int test_alloc2(void __user *argp)
{
	struct event_filter __user *user_filter = argp;
	struct event_filter tmp;
	int *one, *two;

	if (copy_from_user(&tmp, user_filter, sizeof(tmp)))
		return -EFAULT;

	if (tmp.nevents > 300)
		return -E2BIG;

	one = kzalloc_objs(*one, tmp.nevents);
	if (!one)
		return -ENOMEM;

	two = one;

	__smatch_buf_size(one);
	__smatch_buf_size(two);

	return 0;
}

/*
 * check-name: smatch: alloc #2
 * check-command: validation/kernel/build.sh sm_kmalloc2.c
 *
 * check-output-start
sm_kmalloc2.c:33 test_alloc2() buf size: 'one' 300 elements, 1200 bytes (rl = 0-1200)[size_var=elem_count tmp.nevents]
sm_kmalloc2.c:34 test_alloc2() buf size: 'two' 300 elements, 1200 bytes (rl = 0-1200)[size_var=elem_count tmp.nevents]
 * check-output-end
 */
