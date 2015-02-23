/*
 * Copyright (C) 2015 Oracle.
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

static void match_strcpy(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest, *src;

	dest = get_argument_from_call_expr(expr->args, 0);
	src = get_argument_from_call_expr(expr->args, 1);
	src = strip_expr(src);
	if (src->type == EXPR_STRING)
		set_state_expr(my_id, dest, alloc_state_str(src->string->data));
}

struct state_list *get_strings(struct expression *expr)
{
	struct state_list *ret = NULL;
	struct smatch_state *state;
	struct sm_state *sm;

	expr = strip_expr(expr);
	if (expr->type == EXPR_STRING) {
		state = alloc_state_str(expr->string->data);
		sm = alloc_sm_state(my_id, expr->string->data, NULL, state);
		add_ptr_list(&ret, sm);
		return ret;
	}

	if (expr->type == EXPR_CONDITIONAL ||
	    expr->type == EXPR_SELECT) {
		struct state_list *true_strings = NULL;
		struct state_list *false_strings = NULL;

		if (known_condition_true(expr->conditional))
			return get_strings(expr->cond_true);
		if (known_condition_false(expr->conditional))
			return get_strings(expr->cond_false);

		true_strings = get_strings(expr->cond_true);
		false_strings = get_strings(expr->cond_false);
		concat_ptr_list((struct ptr_list *)true_strings, (struct ptr_list **)&false_strings);
		free_slist(&true_strings);
		return false_strings;
	}

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return NULL;

	return clone_slist(sm->possible);
}

void register_strings(int id)
{
	my_id = id;

	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strlcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_strcpy, NULL);
}
