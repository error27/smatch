/*
 * Copyright (C) 2014 Oracle.
 * Copyright 2025 Linaro Ltd.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(freed);
STATE(maybe_freed);

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;
	struct expression *call;
	sval_t sval;

	/*
	 * The situation is functions do:
	 * void my_free(void *p)
	 * {
	 *      if (p)
	 *           free(p);
	 * }
	 * Even though there is an if statement, it's still a free
	 * function.
	 */

	if (sm->state != &freed && sm->state != &maybe_freed)
		return &undefined;

	call = get_this_fn_call();
#if 0
	/*
	 * The problem is that we sometimes know that kref_put() isn't going
	 * to call kfree.  The kref_put() thing is only a MAYBE_FREED because
	 * it's refcounted.  And ideally we would check if the refcount was
	 * bumped in the callers.
	 */
	/* In fake kref_put() everything gets promoted to &free.  :P */
	if (call && sym_name_is("kref_put", call->fn))
		return &freed;
#endif

	/*
	 * Hopefully, things should be merged at the if statement level.
	 * The issue here is that there was a bug so Smatch thought we only
	 * passed NULL pointers to this function so they always got turned
	 * into frees.
	 *
	 */
	if (call)
		return &undefined;

	/* The parent is NULL */
	if (parent_is_null_var_sym(sm->name, sm->sym))
		return sm->state;

	/* The pointer is NULL */
	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (state && estate_get_single_value(state, &sval) && sval.value == 0)
		return sm->state;

	/* The pointer is an error pointer */
	if (is_err_ptr_name_sym(sm->name, sm->sym))
		return sm->state;

	return &undefined;
}

static int get_freed_type(struct sm_state *sm)
{
	if (sm->state == &freed)
		return PARAM_FREED;

	if (slist_has_state(sm->possible, &freed) ||
	    slist_has_state(sm->possible, &maybe_freed))
		return MAYBE_FREED;

	return 0;
}

static void return_freed_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	int type;

	type = get_freed_type(sm);
	if (!type)
		return;

	if (on_atomic_dec_path())
		type = MAYBE_FREED;

	sql_insert_return_states(return_id, return_ranges, type, param,
				 printed_name, "");
}

static void match_free_helper(struct expression *expr, const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct symbol *orig_sym;
	char *orig_name;
	int param;

	orig_name = get_param_var_sym_var_sym_early(name, sym, NULL, &orig_sym);
	if (!orig_name || !orig_sym)
		return;
	param = get_param_key_from_var_sym(orig_name, orig_sym, NULL, NULL);
	if (param < 0)
		return;

	set_state(my_id, orig_name, orig_sym, state);
}

static void match_free(struct expression *expr, const char *name, struct symbol *sym)
{
	match_free_helper(expr, name, sym, &freed);
}

static void match_maybe_free(struct expression *expr, const char *name, struct symbol *sym)
{
	match_free_helper(expr, name, sym, &maybe_freed);
}

void register_free_return_states(int id)
{
	my_id = id;

	add_free_hook(match_free);
	add_maybe_free_hook(match_maybe_free);

	add_return_info_callback(my_id, return_freed_callback);
	add_unmatched_state_hook(my_id, &unmatched_state);
}
