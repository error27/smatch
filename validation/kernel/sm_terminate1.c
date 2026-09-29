#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_copy_from_user(void __user *src)
{
	char buf[32];

	copy_from_user(buf, src, sizeof(buf));
	strlen(buf);

	memset(buf, 0, sizeof(buf));
	__smatch_states("unterm");
	copy_from_user(buf, src, sizeof(buf) - 1);
	__smatch_states("unterm");
	strlen(buf);

	copy_from_user(buf, src, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	__smatch_states("unterm");
	strlen(buf);

	return 0;
}

/*
 * check-name: smatch: unterminated copy_from_user() string
 * check-command: validation/kernel/build.sh sm_terminate1.c
 *
 * check-output-start
sm_terminate1.c:10 sm_terminate_copy_from_user() warn: unterminated user string: 'buf'
 * check-output-end
 */
