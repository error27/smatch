#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_test_last_byte(void __user *src, size_t size)
{
	char buffer[16];
	char *string = buffer;
	char bad[16];

	if (!size || size > sizeof(buffer))
		return -1;
	copy_from_user(string, src, size);
	if (string[size - 1] != '\0')
		return -1;
	strlen(string);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: last byte NUL test
 * check-command: validation/kernel/build.sh sm_terminate14.c
 *
 * check-output-start
sm_terminate14.c:19 sm_terminate_test_last_byte() warn: unterminated user string: 'bad'
 * check-output-end
 */
