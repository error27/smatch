#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/netlink.h>
#include "../../check_debug.h"

int sm_terminate_string_functions(const void __user *src, size_t len,
				  const struct nlattr *nla)
{
	char *string;
	char *bad;
	char buf[16];

	bad = memdup_user(src, len);
	if (IS_ERR(bad))
		return PTR_ERR(bad);
	strlen(bad);

	string = nla_strdup(nla, GFP_KERNEL);
	if (!string)
		return -1;
	strlen(string);
	kfree(string);

	copy_from_user(buf, src, sizeof(buf));
	strscpy(buf, "ok", sizeof(buf));
	strlen(buf);

	memset(buf, 0, sizeof(buf));
	if (nla_strscpy(buf, nla, sizeof(buf)) < 0)
		return -1;
	strlen(buf);

	copy_from_user(buf, src, sizeof(buf));
	snprintf(buf, sizeof(buf), "%s", "ok");
	strlen(buf);

	copy_from_user(buf, src, sizeof(buf));
	scnprintf(buf, sizeof(buf), "%s", "ok");
	strlen(buf);

	return 0;
}

/*
 * check-name: smatch: termination string functions
 * check-command: validation/kernel/build.sh sm_terminate9.c
 *
 * check-output-start
sm_terminate9.c:18 sm_terminate_string_functions() warn: unterminated user string: 'bad'
 * check-output-end
 */
