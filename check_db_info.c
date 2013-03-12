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
static struct ptr_list *backup;

static void add_return_range(struct range_list *rl)
{
	rl = cast_rl(cur_func_return_type(), rl);
	if (!return_ranges) {
		return_ranges = rl;
		return;
	}
	return_ranges = rl_union(return_ranges, rl);
}

static void match_return(struct expression *ret_value)
{
	struct range_list *rl;
	struct symbol *type = cur_func_return_type();

	ret_value = strip_expr(ret_value);
	if (!ret_value)
		return;

	if (get_implied_rl(ret_value, &rl))
		add_return_range(rl);
	else
		add_return_range(alloc_whole_rl(type));
}

static void match_end_func(struct symbol *sym)
{
	if (!return_ranges)
		return;
	sql_insert_return_values(show_rl(return_ranges));
	return_ranges = NULL;
}

static void match_save_states(struct expression *expr)
{
	__add_ptr_list(&backup, return_ranges, 0);
	return_ranges = NULL;
}

static void match_restore_states(struct expression *expr)
{
	return_ranges = last_ptr_list(backup);
	delete_ptr_list_last(&backup);
}

void check_db_info(int id)
{
	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);
}
