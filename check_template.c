/*
 * sparse/check_template.c
 *
 * Copyright (C) 20XX Your Name.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * This is just a template check.  It's designed for teaching
 * only and doesn't even work.
 *
 * First of all, it's best if you lower your expectations from finding
 * errors to just finding suspicious code.  There tends to be a lot
 * of false positives so having low expectations helps.
 *
 * For this test let's look for functions that return a negative value
 * with a semaphore held.
 *
 * This test could be a lot better if it handled the stuff like this:
 * ret = -ENOMEM;
 * return ret;
 * The correct way to handle that is to let smatch_extra store the
 * value of ret.  Then to use a *future* version of smatch that has
 * the get_possible_states() function.  The possible states will 
 * be saved in merge_slist().
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"  // blast this was supposed to be internal only stuff

static int my_id;

STATE(lock);
STATE(unlock);

static void match_call(struct expression *expr)
{
	char *fn_name;
	struct expression *sem_expr;
	char *sem_name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (strcmp(fn_name, "down") && strcmp(fn_name, "up"))
		return;

	sem_expr = get_argument_from_call_expr(expr->args, 0);
	sem_name = get_variable_from_expr_simple(sem_expr, NULL);
	if (strcmp(fn_name, "down")) {
		printf("%d %s locked\n", get_lineno(), sem_name);
		set_state(sem_name, my_id, NULL, &lock);
	} else {
		printf("%d %s unlocked\n", get_lineno(), sem_name);
		set_state(sem_name, my_id, NULL, &unlock);
	}
}

static void match_return(struct statement *stmt)
{
	int ret_val;
	struct state_list *slist;
	struct sm_state *tmp;

	ret_val = get_value(stmt->ret_value);
	if (ret_val == UNDEFINED || ret_val >= 0)
		return;
	
	slist = get_all_states(my_id);

	__print_slist(slist);

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state != &unlock)
			smatch_msg("returned negative with %s semaphore held",
				   tmp->name);
	} END_FOR_EACH_PTR(tmp);
}

void register_template(int id)
{
	my_id = id;
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
}
