/*
 * Copyright (C) 2014 Oracle.
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

static int my_id;
static int my_return_id;
static int my_split_id;

unsigned long nothing_impossible;

STATE(impossible);

int is_impossible_path(void)
{
	if (get_state(my_id, "impossible", NULL) == &impossible)
		return 1;
	return 0;
}

static void handle_compare(struct expression *left, int op, struct expression *right)
{
	int true_impossible = 0;
	int false_impossible = 0;

	left = strip_parens(left);
	while (left && left->type == EXPR_ASSIGNMENT)
		left = strip_parens(left->left);

	if (!possibly_true(left, op, right))
		true_impossible = 1;
	if (!possibly_false(left, op, right))
		false_impossible = 1;

	if (!true_impossible && !false_impossible)
		return;

	set_true_false_states(my_id, "impossible", NULL,
			      true_impossible ? &impossible : NULL,
			      false_impossible ? &impossible : NULL);

	if (inside_loop())
		return;

	set_true_false_states(my_return_id, "impossible", NULL,
			      true_impossible ? &impossible : NULL,
			      false_impossible ? &impossible : NULL);
	if (left || !in_macro(left->pos))
		set_true_false_states(my_split_id, "impossible", NULL,
				      true_impossible ? &impossible : NULL,
				      false_impossible ? &impossible : NULL);
}

static void match_condition(struct expression *expr)
{
	if (expr->type == EXPR_COMPARE)
		handle_compare(expr->left, expr->op, expr->right);
	else
		handle_compare(expr, SPECIAL_NOTEQUAL, zero_expr());
}

void set_true_path_impossible(struct expression *expr)
{
	set_true_false_states(my_id, "impossible", NULL, &impossible, NULL);
	if (inside_loop())
		return;
	set_true_false_states(my_return_id, "impossible", NULL, &impossible, NULL);
	if (expr || !in_macro(expr->pos))
		set_true_false_states(my_split_id, "impossible", NULL, &impossible, NULL);
}

void set_false_path_impossible(struct expression *expr)
{
	set_true_false_states(my_id, "impossible", NULL, NULL, &impossible);
	if (inside_loop())
		return;
	set_true_false_states(my_return_id, "impossible", NULL, NULL, &impossible);
	if (expr || !in_macro(expr->pos))
		set_true_false_states(my_split_id, "impossible", NULL, NULL, &impossible);
}

void set_path_impossible(struct expression *expr)
{
	set_state(my_id, "impossible", NULL, &impossible);

	if (inside_loop())
		return;

	set_state(my_return_id, "impossible", NULL, &impossible);
	if (expr || !in_macro(expr->pos))
		set_state(my_split_id, "impossible", NULL, &impossible);
}

static void match_case(struct expression *expr, struct range_list *rl)
{
	if (rl)
		return;
	set_path_impossible(expr);
}

static void print_impossible_return(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	char value_buf[32];

	if (nothing_impossible)
		return;

	sm = get_sm_state(my_return_id, "impossible", NULL);
	if (!sm || sm->state != &impossible)
		return;
	if (option_debug)
		sm_msg("impossible return.  return_id = %d return ranges = %s", return_id, return_ranges);
	snprintf(value_buf, sizeof(value_buf), "line %d", sm->line);
	sql_insert_return_states(return_id, return_ranges, CULL_PATH, -1, "", value_buf);
}

static void match_thread_stuff(const char *fn, struct expression *expr, void *unused)
{
	nothing_impossible = true;
}

void smatch_impossible(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_case, CASE_HOOK);
}

void smatch_impossible_return(int id)
{
	my_return_id = id;

	add_function_data(&nothing_impossible);
	if (option_project == PROJ_KERNEL) {
		add_function_hook("wait_for_completion", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_timeout", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_io", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_io_timeout", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_interruptible", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_interruptible_timeout", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_killable", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_state", &match_thread_stuff, NULL);
		add_function_hook("wait_for_completion_killable_timeout", &match_thread_stuff, NULL);
		add_function_hook("try_wait_for_completion", &match_thread_stuff, NULL);
	}

	add_split_return_callback(&print_impossible_return);
}

void smatch_impossible_split_return(int id)
{
	my_split_id = id;
}
