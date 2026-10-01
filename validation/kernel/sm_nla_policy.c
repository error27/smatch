#include <net/genetlink.h>

#include "../../check_debug.h"

enum {
	CTA_LABELS,
	CTA_RANGE,
};

static const struct nla_policy policy[] = {
	[CTA_LABELS] = { .type = NLA_NUL_STRING, .len = 16 },
	[CTA_RANGE] = NLA_POLICY_RANGE(NLA_U32, 4, 8),
};
