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

struct x86_event_filter {
	int a, b, c;
	__u32 nevents;
	__u64 events[] __counted_by(nevents);
};

int test_alloc(void __user *argp);
int test_alloc(void __user *argp)
{
	struct event_filter __user *user_filter = argp;
	struct x86_event_filter *filter;
	struct event_filter tmp;
	size_t size;

	if (copy_from_user(&tmp, user_filter, sizeof(tmp)))
		return -EFAULT;

	if (tmp.nevents > 300)
		return -E2BIG;

	size = struct_size(filter, events, tmp.nevents);
	filter = kzalloc(size, GFP_KERNEL_ACCOUNT);
	if (!filter)
		return -ENOMEM;

	filter->nevents = tmp.nevents;

	__smatch_implied(tmp.nevents);
	__smatch_implied(size);
	__smatch_buf_size(filter);
	__smatch_buf_size(filter->events);

	return 0;
}

/*
 * check-name: smatch: buf size #1
 * check-command: validation/kernel/build.sh sm_buf_size1.c
 *
 * check-output-start
sm_buf_size1.c:41 test_alloc() implied: tmp.nevents = '0-300'
sm_buf_size1.c:42 test_alloc() implied: size = '16-2416'
sm_buf_size1.c:43 test_alloc() buf size: 'filter' 151 elements, 2416 bytes (rl = 16-2416)[size_var=byte_count size]
sm_buf_size1.c:44 test_alloc() buf size: 'filter->events' 300 elements, 2400 bytes (rl = 0-2400)
 * check-output-end
 */
