/*
 * Copyright 2023 Linaro Ltd.
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

/*
 * Return true if a function has no calls.  Some built-in calls don't count.
 * This is used for PARAM_USED_STRICT.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static unsigned long has_call;

static int no_calls_callback(void *leaf, int argc, char **argv, char **azColName)
{
	*(bool *)leaf = true;
	return 0;
}

bool call_is_leaf_fn(struct expression *call)
{
	bool leaf = false;

	if (call->fn->type != EXPR_SYMBOL ||
	    !call->fn->symbol) /* to handle __builtin_mul_overflow() apparently */
		return false;

	run_sql(&no_calls_callback, &leaf,
		"select * from return_implies where %s and type = %d;",
		get_static_filter(call->fn->symbol), LEAF_FN);

	return leaf;
}

static void match_caller_info(struct expression *expr)
{
	if (has_call)
		return;
	if (sym_name_is("__builtin_expect", expr->fn))
		return;
	if (call_is_leaf_fn(expr))
		return;
	has_call = true;
}

static void process_states(void)
{
	if (has_call)
		return;
	sql_insert_return_implies(LEAF_FN, -1, "", "");
}

void register_leaf_fn(int id)
{
	my_id = id;

	add_function_data(&has_call);
	add_hook(&match_caller_info, FUNCTION_CALL_HOOK);
	all_return_states_hook(&process_states);
}
