/*
 * sparse/smatch_implied.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Imagine we have this code:
 * foo = 1;
 * if (bar)
 *         foo = 99;
 * else
 *         frob();
 *                   //  <-- point #1
 * if (foo == 99)    //  <-- point #2
 *         bar->baz; //  <-- point #3
 *
 *
 * At point #3 bar is non null and can be dereferenced.
 *
 * It's smatch_implied.c which sets bar to non null at point #2.
 *
 * At point #1 merge_slist() stores the list of states from both
 * the true and false paths.  On the true path foo == 99 and on
 * the false path foo == 1.  merge_slist() sets their my_pools
 * list to show the other states which were there when foo == 99.
 *
 * When it comes to the if (foo == 99) the smatch implied hook
 * looks for all the pools where foo was not 99.  It makes a list
 * of those.
 * 
 * Then for bar (and all the other states) it says, ok bar is a
 * merged state that came from these previous states.  We'll 
 * chop out all the states where it came from a pool where 
 * foo != 99 and merge it all back together.
 *
 * That is the implied state of bar.
 *
 * merge_slist() sets up ->my_pools.
 * merge_sm_state() sets ->pre_merge.
 * If an sm_state is not the same on both sides of a merge, it
 *    gets a ->my_pool set for both sides.  The result is a merged
 *    state that has it's ->pre_merge pointers set.  Merged states
 *    do not immediately have any my_pools set, but maybe will later
 *    when they themselves are merged.
 * A pool is a list of all the states that were set at the time.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

int debug_implied_states = 0;
int option_no_implied = 0;

static int pool_in_pools(struct state_list *pool,
			struct state_list_stack *pools)
{
	struct state_list *tmp;

	FOR_EACH_PTR(pools, tmp) {
		if (tmp == pool)
			return 1;
		if (tmp > pool)
			return 0;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

struct sm_state *remove_my_pools(struct sm_state *sm,
				struct state_list_stack *pools, int *modified)
{
	struct sm_state *ret = NULL;
	struct sm_state *left;
	struct sm_state *right;
	int removed = 0;

	if (!sm)
		return NULL;

	if (pool_in_pools(sm->my_pool, pools)) {
		DIMPLIED("removed %s = %s from %d\n", sm->name,
			show_state(sm->state), sm->line);
		*modified = 1;
		return NULL;
	}

	if (!is_merged(sm)) {
		DIMPLIED("kept %s = %s from %d\n", sm->name, show_state(sm->state),
			sm->line);
		return sm;
	}

	DIMPLIED("checking %s = %s from %d\n", sm->name, show_state(sm->state), sm->line);
	left = remove_my_pools(sm->pre_left, pools, &removed);
	right = remove_my_pools(sm->pre_right, pools, &removed);
	if (!removed) {
		DIMPLIED("kept %s = %s from %d\n", sm->name, show_state(sm->state), sm->line);
		return sm;
	}
	*modified = 1;
	if (!left && !right) {
		DIMPLIED("removed %s = %s from %d\n", sm->name, show_state(sm->state), sm->line);
		return NULL;
	}

	if (!left) {
		ret = clone_state(right);
		ret->merged = 1;
		ret->pre_right = right;
		ret->pre_left = NULL;
		ret->my_pool = sm->my_pool;
	} else if (!right) {
		ret = clone_state(left);
		ret->merged = 1;
		ret->pre_left = left;
		ret->pre_right = NULL;
		ret->my_pool = sm->my_pool;
	} else {
		ret = merge_sm_states(left, right);
		ret->my_pool = sm->my_pool;
	}
	DIMPLIED("partial %s = %s from %d\n", sm->name, show_state(sm->state), sm->line);
	return ret;
}

static struct state_list *filter_stack(struct state_list *pre_list,
				struct state_list_stack *stack)
{
	struct state_list *ret = NULL;
	struct sm_state *tmp;
	struct sm_state *filtered_state;
	int modified = 0;

	if (!stack)
		return NULL;

	FOR_EACH_PTR(pre_list, tmp) {
		if (out_of_memory())
			return NULL;
		filtered_state = remove_my_pools(tmp, stack, &modified);
		if (filtered_state && modified)
			add_ptr_list(&ret, filtered_state);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static int is_checked(struct state_list *checked, struct sm_state *sm)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(checked, tmp) {
		if (tmp == sm)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void separate_pools(struct sm_state *sm_state, int comparison, int num,
			int left,
			struct state_list_stack **true_stack,
			struct state_list_stack **false_stack,
			struct state_list **checked)
{
	struct sm_state *s;
	int istrue, isfalse;
	int free_checked = 0;
	struct state_list *checked_states = NULL;
	static int stopper;
 
	if (!sm_state)
		return;

	if (checked == NULL) {
		stopper = 0;
		checked = &checked_states;
		free_checked = 1;
	}
	if (is_checked(*checked, sm_state)) {
		return;
	}
	add_ptr_list(checked, sm_state);
	
	if (stopper++ >= 500) {
		smatch_msg("internal error:  too much recursion going on here");
		return;
	}

	if (sm_state->my_pool) {
		s = get_sm_state_slist(sm_state->my_pool, sm_state->name, sm_state->owner,
				sm_state->sym);

		istrue = !possibly_false(comparison,
					(struct data_info *)s->state->data, num, 
					left);
		isfalse = !possibly_true(comparison,
					(struct data_info *)s->state->data,
					num, left);

		if (debug_implied_states || debug_states) {
			if (istrue && isfalse) {
				printf("'%s = %s' from %d does not exist.\n",
					s->name, show_state(s->state),
					s->line);
			} else if (istrue) {
				printf("'%s = %s' from %d is true.\n",
					s->name, show_state(s->state),
					s->line);
			} else if (isfalse) {
				printf("'%s = %s' from %d is false.\n",
					s->name, show_state(s->state),
					s->line);
			} else {
				printf("'%s = %s' from %d could be true or "
					"false.\n", s->name,
					show_state(s->state), s->line);
			}
		}
		if (istrue) {
			add_pool(true_stack, s->my_pool);
		}
		if (isfalse) {
			add_pool(false_stack, s->my_pool);
		}
	}
	separate_pools(sm_state->pre_left, comparison, num, left, true_stack, false_stack, checked);
	separate_pools(sm_state->pre_right, comparison, num, left, true_stack, false_stack, checked);
	if (free_checked)
		free_slist(checked);
}

static void get_eq_neq(struct sm_state *sm_state, int comparison, int num,
		int left,
		struct state_list *pre_list,
		struct state_list **true_states,
		struct state_list **false_states)
{
	struct state_list_stack *true_stack = NULL;
	struct state_list_stack *false_stack = NULL;

	if (debug_implied_states || debug_states) {
		if (left)
			smatch_msg("checking implications: (%s %s %d)",
				sm_state->name, show_special(comparison), num);
		else
			smatch_msg("checking implications: (%d %s %s)",
				num, show_special(comparison), sm_state->name);
	}

	separate_pools(sm_state, comparison, num, left, &true_stack, &false_stack, NULL);

	DIMPLIED("filtering true stack.\n");
	*true_states = filter_stack(pre_list, false_stack);
	DIMPLIED("filtering false stack.\n");
	*false_states = filter_stack(pre_list, true_stack);
	free_stack(&true_stack);
	free_stack(&false_stack);
	if (debug_implied_states || debug_states) {
		printf("These are the implied states for the true path:\n");
		__print_slist(*true_states);
		printf("These are the implied states for the false path:\n");
		__print_slist(*false_states);
	}
}

static char *get_implication_variable(struct expression *expr, struct symbol **symp)
{
	expr = strip_expr(expr);
	if (expr->type == EXPR_ASSIGNMENT)
		return get_implication_variable(expr->left, symp);
	return get_variable_from_expr(expr, symp);
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
		name = get_implication_variable(expr->left, &sym);
	else 
		name = get_implication_variable(expr->right, &sym);
	if (!name || !sym)
		goto free;
	state = get_sm_state(name, SMATCH_EXTRA, sym);
	if (!state)
		goto free;
	if (!is_merged(state)) {
		DIMPLIED("%d '%s' is not merged.\n", get_lineno(), state->name);
		goto free;
	}
	get_eq_neq(state, expr->op, value, left, __get_cur_slist(), implied_true, implied_false);
	delete_state_slist(implied_true, name, SMATCH_EXTRA, sym);
	delete_state_slist(implied_false, name, SMATCH_EXTRA, sym);
free:
	free_string(name);
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
	if (expr->type == EXPR_ASSIGNMENT) {
		/* most of the time ->my_pools will be empty here because we
 		   just set the state, but if have assigned a conditional
		   function there are implications. */ 
		expr = expr->left;
	}

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	state = get_sm_state(name, SMATCH_EXTRA, sym);
	if (!state)
		goto free;
	if (!is_merged(state)) {
		DIMPLIED("%d '%s' has no pools.\n", get_lineno(), state->name);
		goto free;
	}
	get_eq_neq(state, SPECIAL_NOTEQUAL, 0, 1, __get_cur_slist(), implied_true, implied_false);
	delete_state_slist(implied_true, name, SMATCH_EXTRA, sym);
	delete_state_slist(implied_false, name, SMATCH_EXTRA, sym);
free:
	free_string(name);
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

void get_implications(char *name, struct symbol *sym, int comparison, int num,
		      struct state_list **true_states,
		      struct state_list **false_states)
{
	struct sm_state *sm;

	sm = get_sm_state(name, SMATCH_EXTRA, sym);
	if (!sm)
		return;
	if (slist_has_state(sm->possible, &undefined))
		return;
	get_eq_neq(sm, comparison, num, 1, __get_cur_slist(), true_states, false_states);
}

struct state_list *__implied_case_slist(struct expression *switch_expr,
					struct expression *case_expr,
					struct state_list **raw_slist)
{
	char *name = NULL;
	struct symbol *sym;
	struct sm_state *sm;
	struct sm_state *true_sm;
	struct sm_state *false_sm;
	struct state_list *true_states = NULL;
	struct state_list *false_states = NULL;
	struct state_list *ret = clone_slist(*raw_slist);
	long long val;

	if (!case_expr)
		return ret;
	name = get_variable_from_expr(switch_expr, &sym);
	if (!name || !sym)
		goto free;
	sm = get_sm_state_slist(*raw_slist, name, SMATCH_EXTRA, sym);
	val = get_value(case_expr);
	if (val == UNDEFINED)
		goto free;
	if (sm) {
		get_eq_neq(sm, SPECIAL_EQUAL, val, 1, *raw_slist, &true_states, &false_states);
	}
	
	true_sm = get_sm_state_slist(true_states, name, SMATCH_EXTRA, sym);
	if (!true_sm)
		set_state_slist(&true_states, name, SMATCH_EXTRA, sym, alloc_extra_state(val));
	false_sm = get_sm_state_slist(false_states, name, SMATCH_EXTRA, sym);
      	if (!false_sm)
		set_state_slist(&false_states, name, SMATCH_EXTRA, sym, add_filter(sm?sm->state:NULL, val));
	overwrite_slist(false_states, raw_slist);
	overwrite_slist(true_states, &ret);
	free_slist(&true_states);
	free_slist(&false_states);
free:
	free_string(name);
	return ret;
}

void __extra_match_condition(struct expression *expr);
void register_implications(int id)
{
	add_hook(&implied_states_hook, CONDITION_HOOK);
	add_hook(&__extra_match_condition, CONDITION_HOOK);
}
