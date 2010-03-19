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

static char *show_num(long long num)
{
	static char buff[256];

	if (num < 0) {
		snprintf(buff, 255, "(%lld)", num);
	} else {
		snprintf(buff, 255, "%lld", num);
	}
	buff[255] = '\0';
	return buff;
}

static char *show_ranges_raw(struct range_list *list)
{
	struct data_range *tmp;
	static char full[256];
	int i = 0;

	full[0] = '\0';
	full[255] = '\0';
 	FOR_EACH_PTR(list, tmp) {
		if (i++)
			strncat(full, ",", 254 - strlen(full));
		if (tmp->min == tmp->max) {
			strncat(full, show_num(tmp->min), 254 - strlen(full));
			continue;
		}
		strncat(full, show_num(tmp->min), 254 - strlen(full));
		strncat(full, "-", 254 - strlen(full));
		strncat(full, show_num(tmp->max), 254 - strlen(full));
	} END_FOR_EACH_PTR(tmp);
	return full;
}

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
	rlist = ((struct data_info *)state->data)->value_ranges;
	sm_msg("info: return_value %s", show_ranges_raw(rlist));
}

void check_db_info(int id)
{
	if (!option_print_returns)
		return;
	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
}
