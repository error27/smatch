/*
 * Copyright (C) 2009 Dan Carpenter.
 * Copyright (C) 2019 Oracle.
 * Copyright 2024 Linaro Ltd.
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

#include <ctype.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(locked);
STATE(unlocked);

static void lock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &locked);
}

static void unlock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &unlocked);
}

static void clear_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	if (!get_state(my_id, name, sym))
		return;
	set_state(my_id, name, sym, &undefined);
}

static bool is_EINTR(struct range_list *rl)
{
	sval_t sval;

	if (!rl_to_sval(rl, &sval))
		return false;
	return sval.value == -4;
}

static void check_lock_bool(const char *name, struct symbol *sym)
{
	struct range_list *locked_true = NULL, *locked_false = NULL;
	struct range_list *unlocked_true = NULL, *unlocked_false = NULL;
	struct stree *stree, *orig;
	struct sm_state *return_sm;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	sval_t sval;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);
		if (!sm)
			goto swap_stree;

		if (parent_is_gone_var_sym(sm->name, sm->sym))
			goto swap_stree;

		if (sm->state != &locked && sm->state != &unlocked)
			goto swap_stree;

		if (sm->state == &unlocked && is_EINTR(estate_rl(return_sm->state)))
			goto swap_stree;

		if (estate_get_single_value(return_sm->state, &sval)) {
			if (sm->state == &locked) {
				if (sval.value)
					add_range(&locked_true, line, line);
				else
					add_range(&locked_false, line, line);
			} else {
				if (sval.value)
					add_range(&unlocked_true, line, line);
				else
					add_range(&unlocked_false, line, line);
			}
		}
swap_stree:
		__swap_cur_stree(orig);
	} END_FOR_EACH_PTR(stree);

	if (locked_true && unlocked_true) {
		sm_warning("inconsistent returns '%s'.", name);
		sm_printf("  Locked on  : %s\n", show_rl(locked_true));
		sm_printf("  Unlocked on: %s\n", show_rl(unlocked_true));
	}
	if (locked_false && unlocked_false) {
		sm_warning("inconsistent returns '%s'.", name);
		sm_printf("  Locked on  : %s\n", show_rl(locked_false));
		sm_printf("  Unlocked on: %s\n", show_rl(unlocked_false));
	}
}

#define NUM_BUCKETS (RET_UNKNOWN + 1)
static void check_lock(const char *name, struct symbol *sym)
{
	struct range_list *locked_lines = NULL;
	struct range_list *unlocked_lines = NULL;
	int locked_buckets[NUM_BUCKETS] = {};
	int unlocked_buckets[NUM_BUCKETS] = {};
	struct stree *stree, *orig;
	struct sm_state *return_sm;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	int bucket;
	int i;

	if (is_locking_primitive_sym(cur_func_sym))
		return;

	if (strchr(name, '$'))
		return;

	if (cur_func_return_type() == &bool_ctype) {
		check_lock_bool(name, sym);
		return;
	}

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		sm = get_sm_state(my_id, name, sym);
		if (!sm)
			goto swap_stree;

		if (parent_is_gone_var_sym(sm->name, sm->sym))
			goto swap_stree;

#if 0
		if (sm_both_locked_and_unlocked(sm) && !both_warned) {
			sm_printf("%s:%lld %s() warn: XXX '%s' both locked and unlocked.\n",
				  get_filename(), line.value, get_function(), sm->name);
			sm_msg("%s: sm = '%s'", __func__, show_sm(sm));
			both_warned = 1;
			goto swap_stree;
		}
#endif

		if (sm->state != &locked && sm->state != &unlocked)
			goto swap_stree;

		if (sm->state == &unlocked && is_EINTR(estate_rl(return_sm->state)))
			goto swap_stree;

		bucket = success_fail_return(estate_rl(return_sm->state));
		if (sm->state == &locked) {
			add_range(&locked_lines, line, line);
			locked_buckets[bucket] = true;
		} else {
			add_range(&unlocked_lines, line, line);
			unlocked_buckets[bucket] = true;
		}
swap_stree:
		__swap_cur_stree(orig);
	} END_FOR_EACH_PTR(stree);


	if (!locked_lines || !unlocked_lines)
		return;

	for (i = 0; i < NUM_BUCKETS; i++) {
		if (locked_buckets[i] && unlocked_buckets[i])
			goto complain;
	}

	if (locked_buckets[RET_FAIL])
		goto complain;

	return;

complain:
	sm_warning("inconsistent returns '%s'.", name);
	sm_printf("  Locked on  : %s\n", show_rl(locked_lines));
	sm_printf("  Unlocked on: %s\n", show_rl(unlocked_lines));
}

static void match_func_end(struct symbol *sym)
{
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, get_all_return_states(), sm) {
		check_lock(sm->name, sm->sym);
	} END_FOR_EACH_SM(sm);
}

void check_inconsistent_locking(int id)
{
	my_id = id;

	add_lock_hook(&lock_hook);
	add_unlock_hook(&unlock_hook);
	add_restore_hook(&unlock_hook);
	add_clear_hook(&clear_hook);

	add_modification_hook(my_id, &set_undefined);

	add_hook(&match_func_end, END_FUNC_HOOK);
}
