#include <net/genetlink.h>
#include <uapi/linux/nl80211.h>

#include "../../check_debug.h"

void nla_data_terminated(struct genl_info *info)
{
	char *name;

	name = nla_data(info->attrs[NL80211_ATTR_IFNAME]);
	__smatch_states("smatch_terminator_xfun");
}

/*
 * check-name: smatch: nla_data() NUL termination
 * check-command: validation/kernel/build.sh sm_nla_data_terminated.c
 *
 * check-output-ignore
 */
