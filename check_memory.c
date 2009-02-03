/*
 * sparse/check_memory.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(allocated);
STATE(isfree);

static void match_assign(struct expression *expr)
{
	struct expression *left, *right;
	char *right_name, *left_name;
	

	left = strip_expr(expr->left);
	left_name = get_variable_from_expr(left, NULL);
	if (!left_name)
		return;

	right = strip_expr(expr->right);

       	right_name = get_variable_from_expr(expr->fn, NULL);
	if (right_name && !strcmp(right_name, "kmalloc")) {
		if (get_state(left_name, my_id, NULL) == &allocated) {
			/* fixme.  malloc can fail and be called twice. */
			/* smatch_msg("possible memory leak."  
			   "  double allocation of %s",
			   name); */
		}
		set_state(left_name, my_id, NULL, &allocated);
		free_string(right_name);
		return;
	}
	free_string(right_name);

	
	if (get_state(left_name, my_id, NULL) == &isfree) {
		set_state(left_name, my_id, NULL, &allocated);
	}
}

static void match_kfree(struct expression *expr)
{
	struct expression *ptr_expr;
	struct state_list *slist;
	char *fn_name;
	char *ptr_name;


	fn_name = get_variable_from_expr(expr->fn, NULL);

	if (!fn_name || strcmp(fn_name, "kfree"))
		return;

	ptr_expr = get_argument_from_call_expr(expr->args, 0);
	ptr_name = get_variable_from_expr(ptr_expr, NULL);
	slist = get_possible_states(ptr_name, my_id, NULL);
	if (slist_has_state(slist, &isfree)) {
		smatch_msg("double free of %s", ptr_name);
	}
	set_state(ptr_name, my_id, NULL, &isfree);

	free_string(fn_name);
}

void register_memory(int id)
{
	my_id = id;
	add_hook(&match_kfree, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
