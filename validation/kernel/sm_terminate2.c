#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_memdup(const void __user *src, unsigned long len)
{
	char *buf;

	buf = memdup_user(src, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	strlen(buf);
	kfree(buf);

	buf = memdup_user(src, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	if (!len) {
		kfree(buf);
		return 0;
	}
	buf[len - 1] = '\0';
	strlen(buf);
	kfree(buf);

	return 0;
}

/*
 * check-name: smatch: unterminated memdup_user() string
 * check-command: validation/kernel/build.sh sm_terminate2.c
 *
 * check-output-start
sm_terminate2.c:13 sm_terminate_memdup() warn: unterminated user string: 'buf'
 * check-output-end
 */
