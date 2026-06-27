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

int test_memcpy(void __user *argp);
int test_memcpy(void __user *argp)
{
	struct event_filter __user *user_filter = argp;
	struct event_filter tmp;
	struct event_filter copy;
	struct event_filter *p;

	if (copy_from_user(&tmp, user_filter, sizeof(tmp)))
		return -EFAULT;

	if (tmp.nevents > 300)
		return -E2BIG;

	memcpy(&copy, &tmp, sizeof(copy));

	__smatch_implied(copy.nevents);

	p = kzalloc(struct_size(p, events, tmp.nevents), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, &tmp, sizeof(*p));
	__smatch_implied(p->nevents);

	return 0;
}


/*
 * check-name: smatch: memcpy #1
 * check-command: validation/kernel/build.sh sm_memcpy1.c
 *
 * check-output-start
sm_memcpy1.c:30 test_memcpy() implied: copy.nevents = '0-300'
sm_memcpy1.c:37 test_memcpy() implied: p->nevents = '0-300'
 * check-output-end
 */
