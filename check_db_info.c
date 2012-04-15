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

static struct range_list *return_ranges;

static void add_return_range(struct range_list *rl)
{
	if (!return_ranges) {
		return_ranges = rl;
		return;
	}
	return_ranges = range_list_union(return_ranges, rl);
}

static void match_return(struct expression *ret_value)
{
	struct range_list *rl;

	ret_value = strip_expr(ret_value);
	if (!ret_value)
		return;

	if (get_implied_range_list(ret_value, &rl)) {
		sm_msg("info: return_value %s", show_ranges(rl));
		add_return_range(rl);
	} else {
		sm_msg("info: return_value unknown");
		add_return_range(whole_range_list());
	}
}

static void match_end_func(struct symbol *sym)
{
	if (!return_ranges)
		return;
	sm_msg("info: function_return_values '%s'", show_ranges(return_ranges));
	return_ranges = NULL;
}

void check_db_info(int id)
{
	if (!option_info)
		return;
	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
