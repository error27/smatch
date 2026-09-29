#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_kzalloc_copy_from_user(void __user *src, size_t count)
{
	char *string;
	char *bad;

	string = kzalloc(count + 1, GFP_KERNEL);
	if (!string)
		return -1;

	copy_from_user(string, src, count);
	strlen(string);
	kfree(string);

	bad = memdup_user(src, count);
	if (IS_ERR(bad))
		return PTR_ERR(bad);
	strlen(bad);
	kfree(bad);

	return 0;
}

/*
 * check-name: smatch: kzalloc bounded copy_from_user() string
 * check-command: validation/kernel/build.sh sm_terminate6.c
 *
 * check-output-start
sm_terminate6.c:22 sm_terminate_kzalloc_copy_from_user() warn: unterminated user string: 'bad'
 * check-output-end
 */
