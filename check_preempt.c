/*
 * Copyright (C) 2018 Oracle.
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
 * This code works with check_preempt_info.c.  The info.c file records
 * return_states (this code handles caller_info states).  The main
 * thing that this code does is it provides get_preempt_count() function
 * the check_scheduling_in_atomic.c file.
 *
 * The preempt count is a counter and when it's non-zero then we are not
 * allowed to call schedule().
 *
 * If we're called with a lock held then we say that preempt is one.  (In
 * real life, it could be higher but this is easier to program).  So if
 * any caller is holding the lock we have preempt = 1.
 *
 * If during the course of parsing the call, the preempt count gets out of
 * sync on one side of a branch statement, then we assume the lower preempt
 * count is correct.  (In other words, we choose to miss some bugs rather
 * than add false postives).
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

int get_preempt_cnt(void)
{
	struct smatch_state *state;

	state = get_state(my_id, "preempt", NULL);
	if (!state)
		return 0;
	return PTR_INT(state->data);
}

void clear_preempt_cnt(void)
{
	struct smatch_state *state;

	state = get_state(my_id, "preempt", NULL);
	if (!state || PTR_INT(state->data) == 0)
		return;
	set_state(my_id, "preempt", NULL, alloc_state_num(0));
}

static unsigned long fn_decrements_preempt;
bool function_decrements_preempt(void)
{
	return fn_decrements_preempt;
}

static int get_start_preempt_cnt(void)
{
	struct stree *orig, *start;
	int ret;

	start = get_start_states();
	orig = __swap_cur_stree(start);

	ret = get_preempt_cnt();

	__swap_cur_stree(orig);
	return ret;
}

static bool passes_MSG_DONTWAIT(struct expression *expr)
{
	struct expression *arg;
	char *macro;

	FOR_EACH_PTR(expr->args, arg) {
		macro = get_macro_name(arg->pos);
		if (macro && strcmp(macro, "MSG_DONTWAIT") == 0)
			return true;
	} END_FOR_EACH_PTR(arg);

	return false;
}

static bool is_no_preempt_caller(void)
{
	char *fn = get_function();

	if (!fn)
		return false;
	if (strcmp(fn, "sg_miter_next") == 0)
		return true;
	return false;
}

static void match_call_info(struct expression *expr)
{
	int start_cnt, cnt;
	int param;
	const char *disables = "";

	if (is_fn_ptr(expr->fn))
		return;
	cnt = get_preempt_cnt();
	if (cnt <= 0)
		return;
	if (passes_MSG_DONTWAIT(expr))
		return;
	if (is_no_preempt_caller())
		return;
	start_cnt = get_start_preempt_cnt();
	if (start_cnt < cnt)
		disables = "<- disables preempt";

	param = get_gfp_param(expr);
	if (param >= 0)
		return;
	sql_insert_caller_info(expr, PREEMPT_ADD, -1, "", disables);
}

static struct smatch_state *merge_func(struct smatch_state *s1, struct smatch_state *s2)
{
	if (__in_function_def) {
		if (PTR_INT(s1->data) > PTR_INT(s2->data))
			return s1;
		return s2;
	}
	if (PTR_INT(s1->data) < PTR_INT(s2->data))
		return s1;
	return s2;
}

static void select_call_info(const char *name, struct symbol *sym, char *key, char *value)
{
	set_state(my_id, "preempt", NULL, alloc_state_num(1));
}

void __preempt_add(void)
{
	set_state(my_id, "preempt", NULL, alloc_state_num(get_preempt_cnt() + 1));
}

void __preempt_sub(void)
{
	fn_decrements_preempt = 1;
	set_state(my_id, "preempt", NULL, alloc_state_num(get_preempt_cnt() - 1));
}

static void match_preempt_count_zero(const char *fn, struct expression *call_expr,
				     struct expression *assign_expr, void *_param)
{
	set_state(my_id, "preempt", NULL, alloc_state_num(0));
}

static void match_preempt_count_non_zero(const char *fn, struct expression *call_expr,
					 struct expression *assign_expr, void *_param)
{
	struct sm_state *sm, *tmp;
	bool possibly_atomic = false;

	sm = get_sm_state(my_id, "preempt", NULL);
	if (!sm)
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state->data) {
			possibly_atomic = true;
			break;
		}
	} END_FOR_EACH_PTR(tmp);

	if (!possibly_atomic)
		return;

	set_state(my_id, "preempt", NULL, alloc_state_num(1));
}

static int set_sleeps(void *_sleeps, int argc, char **argv, char **azColName)
{
	int *fn_sleeps = _sleeps;

	*fn_sleeps = 1;
	return 0;
}

static void silence_duplicates(struct symbol *sym)
{
	int fn_sleeps = 0;

	if (!sym || !sym->ident)
		return;

	run_sql(set_sleeps, &fn_sleeps,
		"select * from return_implies where %s and type = %d;",
		get_static_filter(sym), SLEEP);

	if (fn_sleeps)
		set_state(my_id, "preempt", NULL, alloc_state_num(0));
}

void check_preempt(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&fn_decrements_preempt);
	set_dynamic_states(my_id);
	add_merge_hook(my_id, &merge_func);

	return_implies_exact("preempt_count", int_zero, int_zero, &match_preempt_count_zero, NULL);
	return_implies_exact("preempt_count", int_one, int_max, &match_preempt_count_non_zero, NULL);

	select_caller_info_hook(&select_call_info, PREEMPT_ADD);
	add_hook(&silence_duplicates, AFTER_DEF_HOOK);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
}

