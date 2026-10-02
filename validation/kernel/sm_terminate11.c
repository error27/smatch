#include <linux/sockptr.h>
#include <linux/string.h>
#include "../../check_debug.h"

int sm_terminate_copy_from_sockptr(sockptr_t src, unsigned long len)
{
	char name[16];
	char bad[16];

	if (len >= sizeof(name))
		return -1;

	memset(name, 0, sizeof(name));
	copy_from_sockptr(name, src, len);
	strlen(name);

	copy_from_sockptr(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: copy_from_sockptr() string
 * check-command: validation/kernel/build.sh sm_terminate11.c
 *
 * check-output-start
sm_terminate11.c:18 sm_terminate_copy_from_sockptr() warn: unterminated user string: 'bad'
 * check-output-end
 */
