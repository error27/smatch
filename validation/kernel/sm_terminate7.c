#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

static void sm_terminate_callee(char *string)
{
	strlen(string);
}

int sm_terminate_caller(void __user *src)
{
	char string[16];

	copy_from_user(string, src, sizeof(string));
	strlen(string);
	sm_terminate_callee(string);

	return 0;
}

/*
 * check-name: smatch: unterminated caller info
 * check-command: validation/kernel/build.sh sm_terminate7.c
 *
 * check-output-start
sm_terminate7.c:15 sm_terminate_caller() warn: unterminated user string: 'string'
 * check-output-end
 */
