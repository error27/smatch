/*
 * sparse/check_template.c
 *
 * Copyright (C) 20XX Your Name.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * First of all, it's best if you lower your expectations from finding
 * errors to just finding suspicious code.  There tends to be a lot
 * of false positives so having low expectations helps.
 *
 * For this test let's look for functions that return a negative value
 * with a semaphore held.
 *
 * This is just a template check.  It's designed for teaching
 * only and is deliberately less useful than it could be.
 *
 * This test could be a lot better if it handled the stuff like this:
 * ret = -ENOMEM;
 * return ret;
 * The correct way to handle that is to let smatch_extra store the
 * value of ret.  Then to use a *future* version of smatch that has
 * the get_possible_states(name, SMATCH_EXTRA, sym) function.  The
 * possible states will be saved in merge_slist() in that future version.
 * 
 * Another short coming is that it assumes a function isn't supposed
 * to return negative with a lock held.  Perhaps the function was
 * called with the lock held.  A more complicated script could check that.
 *
 * Also it would be more effective to check for other types of locks
 * especially spinlocks.
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"  // blast this was supposed to be internal only stuff

static int my_id;

STATE(lock);
STATE(unlock);

/*
 * merge_func() can go away when we fix the core to just store all the possible 
 * states.
 *
 * The parameters are passed in alphabetical order with NULL at the beginning
 * of the alphabet.  (s2 is never NULL).
 */

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	if (s1 == NULL)
		return s2;
	return &undefined;

}

static void match_call(struct expression *expr)
{
	char *fn_name;
	struct expression *sem_expr;
	char *sem_name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name || (strcmp(fn_name, "down") && strcmp(fn_name, "up")))
		return;

	sem_expr = get_argument_from_call_expr(expr->args, 0);
	sem_name = get_variable_from_expr(sem_expr, NULL);
	if (!strcmp(fn_name, "down")) {
		set_state(sem_name, my_id, NULL, &lock);
	} else {
		set_state(sem_name, my_id, NULL, &unlock);
	}
	free_string(fn_name);
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
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state != &unlock)
			smatch_msg("returned negative with %s semaphore held",
				   tmp->name);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

void register_template(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
}
