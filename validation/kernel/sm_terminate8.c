#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

struct sm_terminate_args {
	char name[16];
};

extern int btrfs_check_ioctl_vol_args_path(struct sm_terminate_args *args);

int sm_terminate_checked_string(void __user *src)
{
	struct sm_terminate_args args;
	char string[16];
	char bad[16];

	copy_from_user(args.name, src, sizeof(args.name));
	if (btrfs_check_ioctl_vol_args_path(&args))
		return -1;
	strlen(args.name);

	copy_from_user(string, src, sizeof(string));
	if (!memchr(string, 0, sizeof(string)))
		return -1;
	strlen(string);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: checked NUL terminated strings
 * check-command: validation/kernel/build.sh sm_terminate8.c
 *
 * check-output-start
sm_terminate8.c:28 sm_terminate_checked_string() warn: unterminated user string: 'bad'
 * check-output-end
 */
