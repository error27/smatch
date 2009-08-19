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
 * the false path foo == 1.  merge_slist() sets their my_pool
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
 * merge_slist() sets up ->my_pool.  An sm_state only has one ->my_pool and
 *    that is the pool where it was first set.  Implied states sometimes have a 
 *    my_pool to reflect that the code flowed through that path.
 * merge_sm_state() sets ->left and ->right.  (These are the states which were
 *    merged to form the current state.)
 * If an sm_state is not the same on both sides of a merge, it
 *    gets a ->my_pool set for both sides.  The result is a merged
 *    state that has it's ->left and ->right pointers set.  Merged states
 *    do not immediately have any my_pool set, but maybe will later
 *    when they themselves are merged.
 * A pool is a list of all the states that were set at the time.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

#define DIMPLIED(msg...) do { if (debug_implied_states) printf(msg); } while (0)

int debug_implied_states = 0;
int option_no_implied = 0;

static int print_once = 0;

static struct range_list *my_list = NULL;
static struct data_range *my_range;

static struct range_list *tmp_range_list(long num)
{
	__free_ptr_list((struct ptr_list **)&my_list);
	my_range = alloc_range(num, num);
	my_range->min = num;
	my_range->max = num;
	add_ptr_list(&my_list, my_range);
	return my_list;
}

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

	if (sm->nr_children > 5000) {
		if (!print_once++) {
			sm_msg("debug: remove_my_pools %s nr_children %d",
				sm->name, sm->nr_children);
		}
		return NULL;
	}

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
	left = remove_my_pools(sm->left, pools, &removed);
	right = remove_my_pools(sm->right, pools, &removed);
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
		ret->right = right;
		ret->left = NULL;
		ret->my_pool = sm->my_pool;
	} else if (!right) {
		ret = clone_state(left);
		ret->merged = 1;
		ret->left = left;
		ret->right = NULL;
		ret->my_pool = sm->my_pool;
	} else {
		ret = merge_sm_states(left, right);
		ret->my_pool = sm->my_pool;
	}
	ret->implied = 1;
	DIMPLIED("partial %s = %s from %d\n", sm->name, show_state(sm->state), sm->line);
	return ret;
}

static struct state_list *filter_stack(struct state_list *pre_list,
				struct state_list_stack *stack)
{
	struct state_list *ret = NULL;
	struct sm_state *tmp;
	struct sm_state *filtered_state;
	int modified;
	int counter = 0;

	if (!stack)
		return NULL;

	FOR_EACH_PTR(pre_list, tmp) {
		modified = 0;
		filtered_state = remove_my_pools(tmp, stack, &modified);
		if (filtered_state && modified) {
			add_ptr_list(&ret, filtered_state);
			if ((counter++)%10 && out_of_memory())
				return NULL;

		}
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

static void separate_pools(struct sm_state *sm_state, int comparison, struct range_list *vals,
			int left,
			struct state_list_stack **true_stack,
			struct state_list_stack **false_stack,
			struct state_list **checked)
{
	struct sm_state *s;
	int istrue, isfalse;
	int free_checked = 0;
	struct state_list *checked_states = NULL;
 
	if (!sm_state)
		return;

	/* 
	   Sometimes the implications are just too big to deal with
	   so we bail.  Theoretically, implications only get rid of 
	   false positives and don't affect actual bugs.
	*/
	if (sm_state->nr_children > 5000) {
		if (!print_once++) {
			sm_msg("debug: seperate_pools %s nr_children %d",
				sm_state->name, sm_state->nr_children);
		}
		return;
	}

	if (checked == NULL) {
		checked = &checked_states;
		free_checked = 1;
	}
	if (is_checked(*checked, sm_state)) {
		return;
	}
	add_ptr_list(checked, sm_state);
	
	if (sm_state->my_pool) {
		if (is_implied(sm_state)) {
			s = get_sm_state_slist(sm_state->my_pool,
					sm_state->owner, sm_state->name, 
					sm_state->sym);
		} else { 
			s = sm_state;
		}

		istrue = !possibly_false_range_list(comparison,
						(struct data_info *)s->state->data,
						vals, left);
		isfalse = !possibly_true_range_list(comparison,
						(struct data_info *)s->state->data,
						vals, left);
		
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
	separate_pools(sm_state->left, comparison, vals, left, true_stack, false_stack, checked);
	separate_pools(sm_state->right, comparison, vals, left, true_stack, false_stack, checked);
	if (free_checked)
		free_slist(checked);
}

static void get_eq_neq(struct sm_state *sm_state, int comparison, struct range_list *vals,
		int left,
		struct state_list *pre_list,
		struct state_list **true_states,
		struct state_list **false_states)
{
	struct state_list_stack *true_stack = NULL;
	struct state_list_stack *false_stack = NULL;

	if (debug_implied_states || debug_states) {
		if (left)
			sm_msg("checking implications: (%s %s %s)",
				sm_state->name, show_special(comparison), show_ranges(vals));
		else
			sm_msg("checking implications: (%s %s %s)",
				show_ranges(vals), show_special(comparison), sm_state->name);
	}

	separate_pools(sm_state, comparison, vals, left, &true_stack, &false_stack, NULL);

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
	state = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!state)
		goto free;
	if (!is_merged(state)) {
		DIMPLIED("%d '%s' is not merged.\n", get_lineno(), state->name);
		goto free;
	}
	get_eq_neq(state, expr->op, tmp_range_list(value), left, __get_cur_slist(), implied_true, implied_false);
	delete_state_slist(implied_true, SMATCH_EXTRA, name, sym);
	delete_state_slist(implied_false, SMATCH_EXTRA, name, sym);
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
	state = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!state)
		goto free;
	if (!is_merged(state)) {
		DIMPLIED("%d '%s' has no pools.\n", get_lineno(), state->name);
		goto free;
	}
	get_eq_neq(state, SPECIAL_NOTEQUAL, tmp_range_list(0), 1, __get_cur_slist(), implied_true, implied_false);
	delete_state_slist(implied_true, SMATCH_EXTRA, name, sym);
	delete_state_slist(implied_false, SMATCH_EXTRA, name, sym);
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

struct range_list *__get_implied_values(struct expression *switch_expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	struct range_list *ret = NULL;

	name = get_variable_from_expr(switch_expr, &sym);
	if (!name || !sym)
		goto free;
	state = get_state(SMATCH_EXTRA, name, sym);
	if (!state)
		goto free;
	ret = clone_range_list(((struct data_info *)state->data)->value_ranges);
free:
	free_string(name);
	if (!ret)
		add_range(&ret, whole_range.min, whole_range.max);
	return ret;
}

void get_implications(char *name, struct symbol *sym, int comparison, int num,
		      struct state_list **true_states,
		      struct state_list **false_states)
{
	struct sm_state *sm;

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!sm)
		return;
	if (slist_has_state(sm->possible, &undefined))
		return;
	get_eq_neq(sm, comparison, tmp_range_list(num), 1, __get_cur_slist(), true_states, false_states);
}

struct state_list *__implied_case_slist(struct expression *switch_expr,
					struct expression *case_expr,
					struct range_list_stack **remaining_cases,
					struct state_list **raw_slist)
{
	char *name = NULL;
	struct symbol *sym;
	struct sm_state *sm;
	struct sm_state *true_sm;
	struct state_list *true_states = NULL;
	struct state_list *false_states = NULL;
	struct state_list *ret = clone_slist(*raw_slist);
	long long val;
	struct data_range *range;
	struct range_list *vals = NULL;

	name = get_variable_from_expr(switch_expr, &sym);
	if (!name || !sym)
		goto free;
	sm = get_sm_state_slist(*raw_slist, SMATCH_EXTRA, name, sym);
	if (!case_expr) {
		vals = top_range_list(*remaining_cases);
	} else {
		val = get_value(case_expr);
		if (val == UNDEFINED) {
			goto free;
		} else {
			filter_top_range_list(remaining_cases, val);
			range = alloc_range(val, val);
			add_ptr_list(&vals, range);
		}
	}
	if (sm) {
		get_eq_neq(sm, SPECIAL_EQUAL, vals, 1, *raw_slist, &true_states, &false_states);
	}
	
	true_sm = get_sm_state_slist(true_states, SMATCH_EXTRA, name, sym);
	if (!true_sm)
		set_state_slist(&true_states, SMATCH_EXTRA, name, sym, alloc_extra_state_range_list(vals));
	overwrite_slist(true_states, &ret);
	free_slist(&true_states);
	free_slist(&false_states);
free:
	free_string(name);
	return ret;
}

static void match_end_func(struct symbol *sym)
{
	print_once = 0;
}

void __extra_match_condition(struct expression *expr);
void register_implications(int id)
{
	add_hook(&implied_states_hook, CONDITION_HOOK);
	add_hook(&__extra_match_condition, CONDITION_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
