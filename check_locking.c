/*
 * sparse/check_locking.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * For this test let's look for functions that return a negative value
 * with a spinlock held.
 *
 * One short coming is that it assumes a function isn't supposed
 * to return negative with a lock held.  Perhaps the function was
 * called with the lock held.  A more complicated script could check that.
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
	struct expression *spin_expr;
	char *spin_name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name || (strcmp(fn_name, "_spin_lock") && strcmp(fn_name, "_spin_unlock")))
		return;

	spin_expr = get_argument_from_call_expr(expr->args, 0);
	spin_name = get_variable_from_expr(spin_expr, NULL);
	if (!strcmp(fn_name, "_spin_lock")) {
		set_state(spin_name, my_id, NULL, &lock);
	} else {
		set_state(spin_name, my_id, NULL, &unlock);
	}
	free_string(fn_name);
}

static void match_condition(struct expression *expr)
{
	/* __raw_spin_is_locked */
}

static int possibly_negative(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct state_list *slist;
	struct sm_state *tmp;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		return 0;
	slist = get_possible_states(name, SMATCH_EXTRA, sym);
	FOR_EACH_PTR(slist, tmp) {
		int value = 0;
		
		if (tmp->state->data) 
			value =  *(int *)tmp->state->data;

		if (value < 0) {
			return 1;
		}
		1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void match_return(struct statement *stmt)
{
	int ret_val;
	struct state_list *slist;
	struct sm_state *tmp;

	ret_val = get_value(stmt->ret_value);
	if (ret_val >= 0) {
		return;
	}
	if (ret_val == UNDEFINED) {
		if (!possibly_negative(stmt->ret_value))
			return;
	}

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state != &unlock)
			smatch_msg("returned negative with %s spinlock held",
				   tmp->name);
	} END_FOR_EACH_PTR(tmp);
}

void register_locking(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);
}
