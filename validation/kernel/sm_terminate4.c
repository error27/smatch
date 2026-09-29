#include <linux/err.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_strndup_user(const char __user *src, long len)
{
	char *callout_info;
	char *bad;

	callout_info = strndup_user(src, len);
	if (IS_ERR(callout_info))
		return PTR_ERR(callout_info);
	strlen(callout_info);

	bad = memdup_user(src, len);
	if (IS_ERR(bad))
		return PTR_ERR(bad);
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: strndup_user() terminates strings
 * check-command: validation/kernel/build.sh sm_terminate4.c
 *
 * check-output-start
sm_terminate4.c:19 sm_terminate_strndup_user() warn: unterminated user string: 'bad'
 * check-output-end
 */
