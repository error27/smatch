/*
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

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(lock);
STATE(unlock);

static struct smatch_state *get_opposite(struct smatch_state *state)
{
	if (state == &lock)
		return &unlock;
	if (state == &unlock)
		return &lock;
	return NULL;
}

static struct stree *start_states;

static void set_start_state(const char *name, struct smatch_state *start)
{
	struct smatch_state *orig;

	if (!name || !start)
		return;

	if (get_state(my_id, name, NULL))
		return;

	orig = get_state_stree(start_states, my_id, name, NULL);
	if (!orig)
		set_state_stree(&start_states, my_id, name, NULL, start);
	else if (orig != start)
		set_state_stree(&start_states, my_id, name, NULL, &undefined);
}

static struct smatch_state *get_start_state(struct sm_state *sm)
{
	struct smatch_state *orig;

	if (!sm)
		return NULL;

	orig = get_state_stree(start_states, my_id, sm->name, sm->sym);
	if (orig)
		return orig;
	return NULL;
}

static void update_state(const char *member, struct smatch_state *state)
{
	set_start_state(member, get_opposite(state));
	set_state(my_id, member, NULL, state);
}

static void update_expr(struct expression *expr, struct smatch_state *state)
{
	char *member;

	if (!expr)
		return;
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);

	member = get_member_name(expr);
	if (!member)
		return;

	update_state(member, state);
}

static void lock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	update_expr(expr, &lock);
}

static void unlock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	update_expr(expr, &unlock);
}

bool locking_type_is_start_state(void)
{
	struct sm_state *sm;
	int cnt;

	cnt = 0;
	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (++cnt > 1)
			return false;
		if (sm->state != get_start_state(sm))
			return false;
	} END_FOR_EACH_SM(sm);

	return cnt == 1;
}

static int get_db_type(struct sm_state *sm)
{
	if (sm->state == &lock)
		return TYPE_LOCK2;
	if (sm->state == &unlock)
		return TYPE_UNLOCK2;

	return -1;
}

static bool is_clean_transition(struct sm_state *sm)
{
	struct smatch_state *start;

	start = get_start_state(sm);
	if (!start)
		return false;
	if (start == get_opposite(sm->state))
		return true;
	return false;
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	int type;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		type = get_db_type(sm);
		if (local_debug)
			sm_msg("%s: type=%d sm='%s' clean=%d", __func__,
			       type, show_sm(sm), is_clean_transition(sm));
		if (type == -1)
			continue;

		if (!is_clean_transition(sm))
			continue;

		sql_insert_return_states(return_id, return_ranges, type,
					 -2, sm->name, "");
	} END_FOR_EACH_SM(sm);
}

static void db_param_locked(struct expression *expr, int param, char *key, char *value)
{
	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr && expr->type == EXPR_CALL && is_locking_primitive_expr(expr->fn))
		return;

	update_state(key, &lock);
}

static void db_param_unlocked(struct expression *expr, int param, char *key, char *value)
{
	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr && expr->type == EXPR_CALL && is_locking_primitive_expr(expr->fn))
		return;

	update_state(key, &unlock);
}

void register_locking_type(int id)
{
	my_id = id;

	add_function_data((unsigned long *)&start_states);

	add_lock_hook(&lock_hook);
	add_unlock_hook(&unlock_hook);
	add_restore_hook(&unlock_hook);

	select_return_states_hook(TYPE_LOCK2, &db_param_locked);
	select_return_states_hook(TYPE_UNLOCK2, &db_param_unlocked);

	add_split_return_callback(match_return_info);
}
