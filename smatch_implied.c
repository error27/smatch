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
int option_no_implied = 0;

/*
 * We want to find which states have been modified inside a branch.
 * If you have 2 &merged states they could be different states really 
 * and maybe one or both were modified.  We say it is unchanged if
 * the ->state pointers are the same and they belong to the same pools.
 * If they have been modified on both sides of a branch to the same thing,
 * it's still OK to say they are the same, because that means they won't
 * belong to any pools.
 */
static int is_really_same(struct sm_state *one, struct sm_state *two)
{
	struct state_list *tmp1;
	struct state_list *tmp2;

	if (one->state != two->state)
		return 0;

	PREPARE_PTR_LIST(one->my_pools, tmp1);
	PREPARE_PTR_LIST(two->my_pools, tmp2);
	for (;;) {
		if (!tmp1 && !tmp2)
			return 1;
		if (tmp1 < tmp2) {
			return 0;
		} else if (tmp1 == tmp2) {
			NEXT_PTR_LIST(tmp1);
			NEXT_PTR_LIST(tmp2);
		} else {
			return 0;
		}
	}
	FINISH_PTR_LIST(tmp2);
	FINISH_PTR_LIST(tmp1);
	return 1;
}

static int pool_in_pools(struct state_list_stack *pools,
			struct state_list *pool)
{
	struct state_list *tmp;

	FOR_EACH_PTR(pools, tmp) {
		if (tmp == pool)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

struct state_list *clone_states_in_pool(struct state_list *pool,
				struct state_list *cur_slist)
{
	struct sm_state *state;
	struct sm_state *cur_state;
	struct sm_state *tmp;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(pool, state) {
		cur_state = get_sm_state_slist(cur_slist, state->name,
					state->owner, state->sym);
		if (!cur_state)
			continue;
		if (is_really_same(state, cur_state))
			continue;
		if (pool_in_pools(cur_state->all_pools, pool)) {
			tmp = clone_state(state);
			add_ptr_list(&to_slist, tmp);
		}
	} END_FOR_EACH_PTR(state);
	return to_slist;
}

/*
 * merge_implied() takes an implied state and another possibly implied state
 * from another pool.  It checks that the second pool is reachable from 
 * cur_slist then merges the two states and returns the result.
 */
static struct sm_state *merge_implied(struct sm_state *one,
				      struct sm_state *two,
				      struct state_list *pool,
				      struct state_list *cur_slist)
{
	struct sm_state *cur_state;

	cur_state = get_sm_state_slist(cur_slist, two->name, two->owner,
				two->sym);
	if (!cur_state)
		return NULL;  /* this can't actually happen */
	if (!pool_in_pools(cur_state->all_pools, pool))
		return NULL;
	return merge_sm_states(one, two);
}

/*
 * filter() is used to find what states are the same across
 * a series of slists.
 * It takes a **slist and a *filter.  
 * It removes everything from **slist that isn't in *filter.
 * The reason you would want to do this is if you want to 
 * know what other states are true if one state is true.  (smatch_implied).
 */
static void filter(struct state_list **slist, struct state_list *filter,
		   struct state_list *cur_slist)
{
	struct sm_state *s_one, *s_two;
	struct state_list *results = NULL;
	struct sm_state *tmp;

	PREPARE_PTR_LIST(*slist, s_one);
	PREPARE_PTR_LIST(filter, s_two);
	for (;;) {
		if (!s_one || !s_two)
			break;
		if (cmp_tracker(s_one, s_two) < 0) {
			DIMPLIED("removed %s\n", s_one->name);
			NEXT_PTR_LIST(s_one);
		} else if (cmp_tracker(s_one, s_two) == 0) {
			tmp = merge_implied(s_one, s_two, filter, cur_slist);
			if (tmp)
				add_ptr_list(&results, tmp);
			else
				DIMPLIED("removed %s\n", s_one->name);
			NEXT_PTR_LIST(s_one);
			NEXT_PTR_LIST(s_two);
		} else {
			NEXT_PTR_LIST(s_two);
		}
	}
	FINISH_PTR_LIST(s_two);
	FINISH_PTR_LIST(s_one);

	free_slist(slist);
	*slist = results;
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

static void get_eq_neq(struct sm_state *sm_state, int comparison, int num,
		       int left, struct state_list **true_states,
		       struct state_list **false_states)
{
	struct state_list *list;
	struct smatch_state *s;
	struct state_list_stack *true_stack = NULL;
	struct state_list_stack *false_stack = NULL;
	int tf;

	FOR_EACH_PTR(sm_state->my_pools, list) {
		s = get_state_slist(list, sm_state->name, sm_state->owner,
				    sm_state->sym);
		if (s == &merged) {
			free_stack(&true_stack);
			free_stack(&false_stack);
			DIMPLIED("%d '%s' is merged.\n", get_lineno(), 
				 sm_state->name);
			return;
		}
		if (s == &undefined) {
			push_slist(&true_stack, list);
			push_slist(&false_stack, list);
			continue;
		}
		if (left)
			tf = true_comparison(*(int *)s->data,  comparison, num);
		else
			tf = true_comparison(num,  comparison, *(int *)s->data);
		if (tf) {
			push_slist(&true_stack, list);
		} else {
			push_slist(&false_stack, list);
		}
	} END_FOR_EACH_PTR(list);
	*true_states = filter_stack(true_stack);
	*false_states = filter_stack(false_stack);
	free_stack(&true_stack);
	free_stack(&false_stack);
}

static void handle_comparison(struct expression *expr,
			      struct state_list **implied_true,
			      struct state_list **implied_false)
{
	struct symbol *sym;
	char *name;
	struct sm_state *state;
	int value;
	int left = 0;

	value = get_value(expr->left);
	if (value == UNDEFINED) {
		value = get_value(expr->right);
		if (value == UNDEFINED)
			return;
		left = 1;
	}
	if (left)
		name = get_variable_from_expr(expr->left, &sym);
	else 
		name = get_variable_from_expr(expr->right, &sym);
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
	get_eq_neq(state, expr->op, value, left, implied_true, implied_false);
}

static void get_tf_states(struct expression *expr,
			  struct state_list **implied_true,
			  struct state_list **implied_false)
{
	struct symbol *sym;
	char *name;
	struct sm_state *state;

	if (expr->type == EXPR_COMPARE) {
		handle_comparison(expr, implied_true, implied_false);
		return;
	}

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
	get_eq_neq(state, SPECIAL_NOTEQUAL, 0, 1, implied_true, implied_false);
}

static void implied_states_hook(struct expression *expr)
{
	struct sm_state *state;
	struct state_list *implied_true = NULL;
	struct state_list *implied_false = NULL;

	if (option_no_implied)
		return;

	get_tf_states(expr, &implied_true, &implied_false);

	FOR_EACH_PTR(implied_true, state) {
		__set_true_false_sm(state, NULL);
	} END_FOR_EACH_PTR(state);
	free_slist(&implied_true);

	FOR_EACH_PTR(implied_false, state) {
		__set_true_false_sm(NULL, state);
	} END_FOR_EACH_PTR(state);
	free_slist(&implied_false);
}

void register_implications(int id)
{
	add_hook(&implied_states_hook, CONDITION_HOOK);
}
