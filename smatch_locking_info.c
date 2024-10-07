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

STATE(half_locked);
STATE(locked);
STATE(unlocked);

bool IRQs_disabled(void)
{
	if (get_state(my_id, "irq", NULL) == &locked)
		return true;
	return false;
}

static void lock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &locked);
}

static void unlock_hook(struct lock_info *info, struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &unlocked);
}

static struct stree *printed;
static void call_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	int locked_type = 0;

	if (sm->state == &locked)
		locked_type = LOCK2;
	else if (slist_has_state(sm->possible, &locked) ||
		 slist_has_state(sm->possible, &half_locked))
		locked_type = HALF_LOCKED2;
	else
		return;

	avl_insert(&printed, sm);
	sql_insert_caller_info(call, locked_type, param, printed_name, "");
}

static void match_call_info(struct expression *expr)
{
	struct sm_state *sm;
	const char *name;
	int locked_type;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state == &locked)
			locked_type = LOCK2;
		else if (sm->state == &half_locked ||
			 slist_has_state(sm->possible, &locked))
			locked_type = HALF_LOCKED2;
		else
			continue;

		if (avl_lookup(printed, sm))
			continue;

		if (strcmp(sm->name, "bottom_half") == 0)
			name = "bh";
		else if (strcmp(sm->name, "rcu_read") == 0)
			name = "rcu_read_lock";
		else
			name = sm->name;

		if (strncmp(name, "__fake_param", 12) == 0 ||
		    strchr(name, '$'))
			continue;

		sql_insert_caller_info(expr, locked_type, -2, name, "");
	} END_FOR_EACH_SM(sm);
	free_stree(&printed);
}

static void set_locked(const char *name, struct symbol *sym, char *value)
{
	set_state(my_id, name, sym, &locked);
}

static void set_half_locked(const char *name, struct symbol *sym, char *value)
{
	set_state(my_id, name, sym, &half_locked);
}

void register_locking_info(int id)
{
	my_id = id;

	add_lock_hook(&lock_hook);
	add_unlock_hook(&unlock_hook);
	add_restore_hook(&unlock_hook);

	add_caller_info_callback(my_id, call_info_callback);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);

	select_caller_name_sym(set_locked, LOCK2);
	select_caller_name_sym(set_half_locked, HALF_LOCKED2);
	// check_locking.c records UNLOCKED but what is the point of that?
}
