#include <linux/fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

ssize_t sm_user_ptr_set(void __user *src, size_t count)
{
	char buf[32];
	loff_t pos = 0;
	ssize_t ret;

	ret = simple_write_to_buffer(buf, sizeof(buf), &pos, src, count);
	if (ret != count)
		return ret;
	strlen(buf);

	return 0;
}

/*
 * check-name: smatch: user pointer set at zero offset
 * check-command: validation/kernel/build.sh sm_user_ptr_set1.c
 *
 * check-output-start
sm_user_ptr_set1.c:15 sm_user_ptr_set() warn: unterminated user string: 'buf'
 * check-output-end
 */
