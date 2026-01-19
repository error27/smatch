#include "smatch.h"

struct non_null_ctltable_elems {
	const char *name;
	const int len;
};

static struct non_null_ctltable_elems non_null_elems[] = {
	{.name = "->procname", .len = 10},
	{.name = "->proc_handler", .len = 14},
};

static void match_ctl_table_array_sentinel(struct expression *expr)
{
	char *member_name = NULL;

	if (!expr)
		return;

	member_name = get_member_name(expr);
	if (!member_name)
		return;

	if (strncmp(member_name, "(struct ctl_table)", 18) != 0)
		return;

	for (int i = 0 ; i < ARRAY_SIZE(non_null_elems) ; ++i) {
		if (strncmp(member_name + 18, non_null_elems[i].name, non_null_elems[i].len) == 0) {
			sm_warning ("(struct ctl_table)%s cannot be NULL. Expression : %s",
				    non_null_elems[i].name, expr_to_str(expr));
			return;
		}
	}

	return;
}

void check_sentinel_ctltable(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	if (!option_spammy)
		return;

	add_hook(&match_ctl_table_array_sentinel, CONDITION_HOOK);
}
