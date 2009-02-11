/*
 * sparse/smatch_implied.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * Imagine we have this code:
 * foo = 0;
 * if (bar)
 *         foo = 1;
 *                   //  <-- point #1
 * else
 *         frob();
 *                   //  <-- point #2
 * if (foo == 1)     //  <-- point #3
 *         bar->baz; //  <-- point #4
 *
 * Currently (Oct 2008) in smatch when we merge bar states
 * null and nonnull, at point #2, the state becomes undefined.
 * As a result we get an error at point #3.
 *
 * The idea behind "implied state pools" is to fix that.
 *
 * The implied pools get created in merge_slist().  Whatever 
 * is unique to one slist being merged gets put into a pool.
 *
 * If we set a state that removes it from all pools.
 * 
 * When we come to an if statement where "foo" has some pools
 * associated we take all the pools where "foo == 1" and keep
 * all the states that are consistent across those pools.
 *
 * The point of doing this is to turn an undefined state into
 * a defined state.  This hopefully gets rid of some false positives.
 * What it doesn't do is find new errors that were
 * missed before.
 * 
 * There are quite a few implementation details I haven't figured
 * out.  How do you create implied state pools inside a 
 * complex condition?  How do you determine what is implied 
 * from a complex condition?  The initial patch is extremely rudimentary...
 */

#include "smatch.h"
#include "smatch_slist.h"

/*
 * This function gets all the states which are implied by a non zero value.
 * So for example for the code:
 * if (c) {
 * We would want to know what was implied by c is non zero.
 */ 

static struct state_list *get_non_zero_filtered(struct sm_state *sm_state)
{
	struct state_list *list;
	struct smatch_state *s;
	struct state_list *ret = NULL;

	FOR_EACH_PTR(sm_state->pools, list) {
		s = get_state_slist(list, sm_state->name, sm_state->owner,
				    sm_state->sym);
		if (s == &undefined) {
			del_slist(&ret);
			return NULL;
		}
		if (s->data && *(int *)s->data != 0) {
			if (!ret)
				ret = clone_slist(list);
			else
				filter(&ret, list);
		}
	} END_FOR_EACH_PTR(list);
	return ret;
}

/*
 * What are the implications if (foo == num) ...
 */

static struct state_list *get_equals_filtered(struct sm_state *sm_state, int num)
{
	struct state_list *list;
	struct smatch_state *s;
	struct state_list *ret = NULL;

	FOR_EACH_PTR(sm_state->pools, list) {
		s = get_state_slist(list, sm_state->name, sm_state->owner,
				    sm_state->sym);
		if (s == &undefined) {
			printf("undefined\n");
			del_slist(&ret);
			return NULL;
		}
		if (s->data && *(int *)s->data == num) {
			printf("here\n");

			if (!ret)
				ret = clone_slist(list);
			else
				filter(&ret, list);
		}
	} END_FOR_EACH_PTR(list);
	return ret;
}


/*
 * This condition hook is very connected to smatch_extra.c.
 * It's registered there.
 */

void __implied_states_hook(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	struct sm_state *state;
	struct state_list *implied_true;
	struct state_list *implied_false;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		return;
	state = __get_sm_state(name, SMATCH_EXTRA, sym);
	if (!state)
		return;
	if (!state->pools)
		return;
	implied_true = get_non_zero_filtered(state);
	implied_false = get_equals_filtered(state, 0);
	if (debug_states) {
		printf("Setting the following implied states for the true path.\n");
		__print_slist(implied_true);
	}

	/* FIXME.  We lose the ->pools by doing this. */
	FOR_EACH_PTR(implied_true, state) {
		set_true_false_states(state->name, state->owner, state->sym,
				state->state, NULL);
	} END_FOR_EACH_PTR(state);

	if (debug_states) {
		printf("Setting the following implied states for the false path.\n");
		__print_slist(implied_false);
	}

	FOR_EACH_PTR(implied_false, state) {
		set_true_false_states(state->name, state->owner, state->sym,
				NULL, state->state);
	} END_FOR_EACH_PTR(state);

	free_string(name);
}
