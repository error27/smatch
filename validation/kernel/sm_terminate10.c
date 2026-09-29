#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

static int rtw_debugfs_copy_from_user(char tmp[], int size,
				      const char __user *buffer, size_t count,
				      int num)
{
	int tmp_len;

	memset(tmp, 0, size);
	if (count < num)
		return -EFAULT;
	tmp_len = count > size - 1 ? size - 1 : count;
	if (copy_from_user(tmp, buffer, tmp_len))
		return -EFAULT;
	tmp[tmp_len] = '\0';

	return 0;
}

int sm_terminate_xfun(const char __user *src, size_t count)
{
	char tmp[16];
	char bad[16];

	if (rtw_debugfs_copy_from_user(tmp, sizeof(tmp), src, count, 1))
		return -1;
	strlen(tmp);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: cross-function NUL terminator
 * check-command: validation/kernel/build.sh sm_terminate10.c
 *
 * check-output-start
sm_terminate10.c:31 sm_terminate_xfun() warn: unterminated user string: 'bad'
 * check-output-end
 */
