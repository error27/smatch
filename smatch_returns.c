/*
 * smatch/smatch_returns.c
 *
 * Copyright (C) 2011 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

struct return_states_callback {
	void (*callback)(struct state_list *slist);
};
ALLOCATOR(return_states_callback, "return states callbacks");
DECLARE_PTR_LIST(callback_list, struct return_states_callback);
static struct callback_list *callback_list;

static struct state_list *all_return_states;

void all_return_states_hook(void (*callback)(struct state_list *slist))
{
	struct return_states_callback *rs_cb = __alloc_return_states_callback(0);

	rs_cb->callback = callback;
	add_ptr_list(&callback_list, rs_cb);
}

static void call_hooks()
{
	struct return_states_callback *rs_cb;

	FOR_EACH_PTR(callback_list, rs_cb) {
		rs_cb->callback(all_return_states);
	} END_FOR_EACH_PTR(rs_cb);
}

static void match_return(struct expression *ret_value)
{
	if (__inline_fn)
		return;
	merge_slist(&all_return_states, __get_cur_slist());
}

static void match_end_func(struct symbol *sym)
{
	if (__inline_fn)
		return;
	merge_slist(&all_return_states, __get_cur_slist());
	call_hooks();
	free_slist(&all_return_states);
}

void register_returns(int id)
{
	my_id = id;

	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
