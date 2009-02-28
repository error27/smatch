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

#define EQUALS 0
#define NOTEQUALS 1

/*
 * What are the implications if (foo == num) ...
 */

static struct state_list_stack *get_eq_neq(struct sm_state *sm_state,
					int eq_neq, int num)
{
	struct state_list *list;
	struct smatch_state *s;
	struct state_list_stack *ret = NULL;

	FOR_EACH_PTR(sm_state->my_pools, list) {
		s = get_state_slist(list, sm_state->name, sm_state->owner,
				    sm_state->sym);
		if (s == &undefined) {
			__free_ptr_list((struct ptr_list **)&ret);	
			return NULL;
		}
		if (s->data && ((eq_neq == EQUALS && *(int *)s->data == num) ||
				(eq_neq == NOTEQUALS && *(int *)s->data != num))) {
			push_slist(&ret, list);
		}
	} END_FOR_EACH_PTR(list);
	return ret;
}

static struct state_list *filter_stack(struct state_list_stack *stack)
{
	struct state_list *tmp;
	struct state_list *ret = NULL;
	int i = 0;

	FOR_EACH_PTR(stack, tmp) {
		if (!i++)
			ret = clone_states_in_pool(tmp, __get_cur_slist());
		else
			filter(&ret, tmp, __get_cur_slist());
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

void __implied_states_hook(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	struct sm_state *state;
	struct state_list_stack *true_pools;
	struct state_list_stack *false_pools;
	struct state_list *implied_true;
	struct state_list *implied_false;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		return;
	state = get_sm_state(name, SMATCH_EXTRA, sym);
	if (!state)
		return;
	if (!state->my_pools)
		return;

	true_pools = get_eq_neq(state, NOTEQUALS, 0);
	false_pools = get_eq_neq(state, EQUALS, 0);
	implied_true = filter_stack(true_pools);
	implied_false = filter_stack(false_pools);
	if (debug_states) {
		printf("Setting the following implied states for the true path.\n");
		__print_slist(implied_true);
	}

	FOR_EACH_PTR(implied_true, state) {
		__set_true_false_sm(state, NULL);
	} END_FOR_EACH_PTR(state);

	if (debug_states) {
		printf("Setting the following implied states for the false path.\n");
		__print_slist(implied_false);
	}

	FOR_EACH_PTR(implied_false, state) {
		__set_true_false_sm(NULL, state);
	} END_FOR_EACH_PTR(state);

	free_string(name);
}

void register_implications(int id)
{
	add_hook(&__implied_states_hook, CONDITION_HOOK);
}
