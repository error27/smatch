#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

int sm_terminate_strnlen(void __user *src, size_t size)
{
	char one[16], two[16], three[16], four[16];
	char bad[16];
	size_t len;

	if (!size || size > sizeof(one))
		return -1;

	copy_from_user(one, src, size);
	if (strnlen(one, size) == size)
		return -1;
	strlen(one);

	copy_from_user(two, src, size);
	if (strnlen(two, size) != size)
		strlen(two);

	copy_from_user(three, src, size);
	len = strnlen(three, size);
	if (len < size)
		strlen(three);

	copy_from_user(four, src, size);
	len = strnlen(four, size);
	if (len >= size)
		return -1;
	strlen(four);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: strnlen NUL test
 * check-command: validation/kernel/build.sh sm_terminate15.c
 *
 * check-output-start
sm_terminate15.c:35 sm_terminate_strnlen() warn: unterminated user string: 'bad'
 * check-output-end
 */
