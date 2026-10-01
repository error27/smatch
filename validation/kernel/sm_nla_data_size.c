#include <net/genetlink.h>
#include <uapi/linux/nl80211.h>

#include "../../check_debug.h"

enum {
	HWSIM_ATTR_RADIO_NAME,
	HWSIM_ATTR_FRAME,
};

void nla_data_sizes(struct genl_info *info)
{
	char *name;

	name = nla_data(info->attrs[NL80211_ATTR_IFNAME]);
	__smatch_buf_size(name);

	name = nla_data(info->attrs[HWSIM_ATTR_RADIO_NAME]);
	__smatch_buf_size(name);

	name = nla_data(info->attrs[HWSIM_ATTR_FRAME]);
	__smatch_buf_size(name);
}

/*
 * check-name: smatch: nla_data() buffer size
 * check-command: validation/kernel/build.sh sm_nla_data_size.c
 *
 * check-output-start
sm_nla_data_size.c:16 nla_data_sizes() buf size: 'name' 16 elements, 16 bytes (rl = 1-16)
sm_nla_data_size.c:19 nla_data_sizes() buf size: 'name' 0 elements, 0 bytes
sm_nla_data_size.c:22 nla_data_sizes() buf size: 'name' 2304 elements, 2304 bytes (rl = 0-2304)
 * check-output-end
 */
