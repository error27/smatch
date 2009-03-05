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

int debug_implied_states = 0;

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
			free_stack(&ret);
			DIMPLIED("%d '%s' is undefined\n", get_lineno(), 
				 sm_state->name);
			return NULL;
		}
		if (s->data && ((eq_neq == EQUALS && *(int *)s->data == num) ||
				(eq_neq == NOTEQUALS && *(int *)s->data != num))) {
			DIMPLIED("added pool where %s is %s\n", sm_state->name,
				 show_state(s)); 
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
		if (!i++) {
			ret = clone_states_in_pool(tmp, __get_cur_slist());
			if (debug_implied_states) {
				printf("The first implied pool is:\n");
				__print_slist(ret);
			}
		} else {
			filter(&ret, tmp, __get_cur_slist());
			DIMPLIED("filtered\n");
   		}
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
	if (!name || !sym) {
		free_string(name);
		return;
	}
	state = get_sm_state(name, SMATCH_EXTRA, sym);
	free_string(name);
	if (!state)
		return;
	if (!state->my_pools) {
		DIMPLIED("%d '%s' has no pools.\n", get_lineno(), state->name);
		return;
	}

	if (debug_implied_states) {
		printf("%s has the following possible states:\n", state->name);
		__print_slist(state->possible);
	}

	DIMPLIED("Gettin the implied states for (%s != 0)\n", state->name);
	true_pools = get_eq_neq(state, NOTEQUALS, 0);
	DIMPLIED("There are %s implied pools for (%s != 0).\n", (true_pools?"some":"no"), state->name);
	implied_true = filter_stack(true_pools);
	if (implied_true && (debug_states || debug_implied_states)) {
		printf("Setting the following implied states for (%s != 0).\n",
		       state->name);
		__print_slist(implied_true);
	}
	DIMPLIED("Gettin the implied states for (%s == 0)\n", state->name);
	false_pools = get_eq_neq(state, EQUALS, 0);
	DIMPLIED("There are %s implied pools for (%s == 0).\n", (true_pools?"some":"no"), state->name);
	implied_false = filter_stack(false_pools);
	if (implied_false && (debug_states || debug_implied_states)) {
		printf("Setting the following implied states for (%s == 0).\n",
		       state->name);
		__print_slist(implied_false);
	}

	FOR_EACH_PTR(implied_true, state) {
		__set_true_false_sm(state, NULL);
	} END_FOR_EACH_PTR(state);
	free_stack(&true_pools);
	free_slist(&implied_true);

	FOR_EACH_PTR(implied_false, state) {
		__set_true_false_sm(NULL, state);
	} END_FOR_EACH_PTR(state);
	free_stack(&false_pools);
	free_slist(&implied_false);
}

void register_implications(int id)
{
	add_hook(&__implied_states_hook, CONDITION_HOOK);
}
