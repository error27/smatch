/*
 * Copyright (C) 2011 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

struct return_states_callback {
	void (*callback)(struct stree *stree);
};
ALLOCATOR(return_states_callback, "return states callbacks");
DECLARE_PTR_LIST(callback_list, struct return_states_callback);
static struct callback_list *callback_list;

static struct stree *all_return_states;
static struct stree_stack *saved_stack;

void all_return_states_hook(void (*callback)(struct stree *stree))
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
	merge_stree_no_pools(&all_return_states, __get_cur_stree());
}

static void match_end_func(struct symbol *sym)
{
	merge_stree(&all_return_states, __get_cur_stree());
	call_hooks();
	free_stree(&all_return_states);
}

static void match_save_states(struct expression *expr)
{
	push_stree(&saved_stack, all_return_states);
	all_return_states = NULL;
}

static void match_restore_states(struct expression *expr)
{
	free_stree(&all_return_states);
	all_return_states = pop_stree(&saved_stack);
}

void register_returns(int id)
{
	my_id = id;

	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);
}
