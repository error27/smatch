/*
 * smatch/check_db_info.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static void match_return(struct expression *ret_value)
{
	struct smatch_state *state;
	long long val;
	struct range_list *rlist;

	if (!ret_value) {
		sm_msg("info: return_value void");
		return;
	}
	if (get_value(ret_value, &val)) {
		sm_msg("info: return_value %lld", val);
		return;
	}
	state = get_state_expr(SMATCH_EXTRA, ret_value);
	if (!state) {
		sm_msg("info: return_value unknown");
		return;
	}
	sm_msg("info: return_value %s", state->name);
}

void check_db_info(int id)
{
	if (!option_info)
		return;
	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
}
