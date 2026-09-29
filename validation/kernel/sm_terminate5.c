#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_bounded_copy_from_user(void __user *src, unsigned long len)
{
	char name[16];
	char bad[16];

	if (len >= sizeof(name))
		return -1;

	memset(name, 0, sizeof(name));
	copy_from_user(name, src, len);
	strlen(name);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: bounded copy_from_user() string
 * check-command: validation/kernel/build.sh sm_terminate5.c
 *
 * check-output-start
sm_terminate5.c:18 sm_terminate_bounded_copy_from_user() warn: unterminated user string: 'bad'
 * check-output-end
 */
