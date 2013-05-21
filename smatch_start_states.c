/*
 * sparse/smatch_start_states.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Store the states at the start of the function because this is something that
 * is used in a couple places.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static struct state_list *start_states;
static struct state_list_stack *saved_stack;
static void save_start_states(struct statement *stmt)
{
	start_states = clone_slist(__get_cur_slist());
}

static void match_save_states(struct expression *expr)
{
	push_slist(&saved_stack, start_states);
	start_states = NULL;
}

static void match_restore_states(struct expression *expr)
{
	free_slist(&start_states);
	start_states = pop_slist(&saved_stack);
}

static void match_end_func(void)
{
	free_slist(&start_states);
}

struct state_list *get_start_states(void)
{
	return start_states;
}

void register_start_states(int id)
{
	my_id = id;

	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_hook(&match_save_states, INLINE_FN_START);
	add_hook(&match_restore_states, INLINE_FN_END);
	add_hook(&match_end_func, END_FUNC_HOOK);
}

