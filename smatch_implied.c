/*
 * Copyright (C) 2008 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
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
 * the false path foo == 1.  merge_slist() sets their pool
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
 * merge_slist() sets up ->pool.  An sm_state only has one ->pool and
 *    that is the pool where it was first set.  The my pool gets set when
 *    code paths merge.  States that have been set since the last merge do
 *    not have a ->pool.
 * merge_sm_state() sets ->left and ->right.  (These are the states which were
 *    merged to form the current state.)
 * a pool:  a pool is an slist that has been merged with another slist.
 */

#include <sys/time.h>
#include <time.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

char *implied_debug_msg;
#define DIMPLIED(msg...) do { if (option_debug_implied) printf(msg); } while (0)

int option_debug_implied = 0;

/*
 * tmp_range_list():
 * It messes things up to free range list allocations.  This helper fuction
 * lets us reuse memory instead of doing new allocations.
 */
static struct range_list *tmp_range_list(struct symbol *type, long long num)
{
	static struct range_list *my_list = NULL;
	static struct data_range *my_range;

	__free_ptr_list((struct ptr_list **)&my_list);
	my_range = alloc_range(ll_to_sval(num), ll_to_sval(num));
	add_ptr_list(&my_list, my_range);
	return my_list;
}

static void print_debug_tf(struct sm_state *sm, int istrue, int isfalse)
{
	if (!option_debug_implied && !option_debug)
		return;

	if (istrue && isfalse) {
		printf("'%s = %s' from %d does not exist.\n", sm->name,
			show_state(sm->state), sm->line);
	} else if (istrue) {
		printf("'%s = %s' from %d is true. [stree %d]\n", sm->name, show_state(sm->state),
			sm->line, get_stree_id(sm->pool));
	} else if (isfalse) {
		printf("'%s = %s' from %d is false. [stree %d]\n", sm->name, show_state(sm->state),
			sm->line, get_stree_id(sm->pool));
	} else {
		printf("'%s = %s' from %d could be true or false. [stree %d]\n", sm->name,
			show_state(sm->state), sm->line, get_stree_id(sm->pool));
	}
}

/*
 * add_pool() adds a slist to *pools. If the slist has already been
 * added earlier then it doesn't get added a second time.
 */
void add_pool(struct stree_stack **pools, struct stree *new)
{
	struct stree *tmp;

	FOR_EACH_PTR(*pools, tmp) {
		if (tmp < new)
			continue;
		else if (tmp == new) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(pools, new);
}

/*
 * If 'foo' == 99 add it that pool to the true pools.  If it's false, add it to
 * the false pools.  If we're not sure, then we don't add it to either.
 */
static void do_compare(struct sm_state *sm, int comparison, struct range_list *rl,
			struct stree_stack **true_stack,
			struct stree_stack **false_stack)
{
	struct sm_state *s;
	int istrue;
	int isfalse;
	struct range_list *var_rl;

	if (!sm->pool)
		return;

	if (is_implied(sm)) {
		s = get_sm_state_stree(sm->pool,
				sm->owner, sm->name,
				sm->sym);
	} else {
		s = sm;
	}

	if (!s) {
		if (option_debug_implied || option_debug)
			sm_msg("%s from %d, has borrowed implications.",
				sm->name, sm->line);
		return;
	}

	var_rl = cast_rl(rl_type(rl), estate_rl(s->state));

	istrue = !possibly_false_rl(var_rl, comparison, rl);
	isfalse = !possibly_true_rl(var_rl, comparison, rl);

	print_debug_tf(s, istrue, isfalse);

	if (istrue)
		add_pool(true_stack, s->pool);

	if (isfalse)
		add_pool(false_stack, s->pool);
}

static int pool_in_pools(struct stree *pool,
			 const struct stree_stack *pools)
{
	struct stree *tmp;

	FOR_EACH_PTR(pools, tmp) {
		if (tmp == pool)
			return 1;
		if (tmp > pool)
			return 0;
	} END_FOR_EACH_PTR(tmp);
	return 0;
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

/*
 * separate_pools():
 * Example code:  if (foo == 99) {
 *
 * Say 'foo' is a merged state that has many possible values.  It is the combination
 * of merges.  separate_pools() iterates through the pools recursively and calls
 * do_compare() for each time 'foo' was set.
 */
static void separate_pools(struct sm_state *sm, int comparison, struct range_list *rl,
			struct stree_stack **true_stack,
			struct stree_stack **false_stack,
			struct state_list **checked)
{
	int free_checked = 0;
	struct state_list *checked_states = NULL;

	if (!sm)
		return;

	/*
	   Sometimes the implications are just too big to deal with
	   so we bail.  Theoretically, bailing out here can cause more false
	   positives but won't hide actual bugs.
	*/
	if (sm->nr_children > 4000) {
		if (option_debug || option_debug_implied) {
			static char buf[1028];
			snprintf(buf, sizeof(buf), "debug: separate_pools: nr_children over 4000 (%d). (%s %s)",
				 sm->nr_children, sm->name, show_state(sm->state));
			implied_debug_msg = buf;
		}
		return;
	}

	if (checked == NULL) {
		checked = &checked_states;
		free_checked = 1;
	}
	if (is_checked(*checked, sm))
		return;
	add_ptr_list(checked, sm);

	do_compare(sm, comparison, rl, true_stack, false_stack);

	separate_pools(sm->left, comparison, rl, true_stack, false_stack, checked);
	separate_pools(sm->right, comparison, rl, true_stack, false_stack, checked);
	if (free_checked)
		free_slist(checked);
}

struct sm_state *filter_pools(struct sm_state *sm,
			      const struct stree_stack *remove_stack,
			      const struct stree_stack *keep_stack,
			      int *modified)
{
	struct sm_state *ret = NULL;
	struct sm_state *left;
	struct sm_state *right;
	int removed = 0;

	if (!sm)
		return NULL;

	if (sm->nr_children > 4000) {
		if (option_debug || option_debug_implied) {
			static char buf[1028];
			snprintf(buf, sizeof(buf), "debug: %s: nr_children over 4000 (%d). (%s %s)",
				 __func__, sm->nr_children, sm->name, show_state(sm->state));
			implied_debug_msg = buf;
		}
		return NULL;
	}

	if (pool_in_pools(sm->pool, remove_stack)) {
		DIMPLIED("removed %s from %d [stree %d]\n", show_sm(sm), sm->line, get_stree_id(sm->pool));
		*modified = 1;
		return NULL;
	}

	if (!is_merged(sm) || pool_in_pools(sm->pool, keep_stack)) {
		DIMPLIED("kept %s from %d [stree %d]\n", show_sm(sm), sm->line, get_stree_id(sm->pool));
		return sm;
	}

	DIMPLIED("checking %s from %d (%d) [stree %d] left = %s [stree %d] right = %s [stree %d]\n",
		 show_sm(sm), sm->line, sm->nr_children, get_stree_id(sm->pool),
		 sm->left ? show_sm(sm->left) : "<none>", sm->left ? get_stree_id(sm->left->pool) : -1,
		 sm->right ? show_sm(sm->right) : "<none>", sm->right ? get_stree_id(sm->right->pool) : -1);
	left = filter_pools(sm->left, remove_stack, keep_stack, &removed);
	right = filter_pools(sm->right, remove_stack, keep_stack, &removed);
	if (!removed) {
		DIMPLIED("kept %s from %d [stree %d]\n", show_sm(sm), sm->line, get_stree_id(sm->pool));
		return sm;
	}
	*modified = 1;
	if (!left && !right) {
		DIMPLIED("removed %s from %d <none> [stree %d]\n", show_sm(sm), sm->line, get_stree_id(sm->pool));
		return NULL;
	}

	if (!left) {
		ret = clone_sm(right);
		ret->merged = 1;
		ret->right = right;
		ret->left = NULL;
		ret->pool = sm->pool;
	} else if (!right) {
		ret = clone_sm(left);
		ret->merged = 1;
		ret->left = left;
		ret->right = NULL;
		ret->pool = sm->pool;
	} else {
		ret = merge_sm_states(left, right);
		ret->pool = sm->pool;
	}
	ret->implied = 1;
	DIMPLIED("partial %s => ", show_sm(sm));
	DIMPLIED("%s from %d [stree %d]\n", show_sm(ret), sm->line, get_stree_id(sm->pool));
	return ret;
}

static int highest_stree_id(struct sm_state *sm)
{
	int left = 0;
	int right = 0;

	if (!sm->left && !sm->right)
		return 0;

	if (sm->left)
		left = get_stree_id(sm->left->pool);
	if (sm->right)
		right = get_stree_id(sm->right->pool);

	if (right > left)
		return right;
	return left;
}

static struct stree *filter_stack(struct sm_state *gate_sm,
				       struct stree *pre_stree,
				       const struct stree_stack *remove_stack,
				       const struct stree_stack *keep_stack)
{
	struct stree *ret = NULL;
	struct sm_state *tmp;
	struct sm_state *filtered_sm;
	int modified;

	if (!remove_stack)
		return NULL;

	FOR_EACH_SM(pre_stree, tmp) {
		if (!tmp->merged)
			continue;
		if (highest_stree_id(tmp) < highest_stree_id(gate_sm)) {
			DIMPLIED("skipping %s.  set before.  %d vs %d\n",
					tmp->name, highest_stree_id(tmp),
					highest_stree_id(gate_sm));
			continue;
		}
		modified = 0;
		filtered_sm = filter_pools(tmp, remove_stack, keep_stack, &modified);
		if (filtered_sm && modified) {
			/* the assignments here are for borrowed implications */
			filtered_sm->name = tmp->name;
			filtered_sm->sym = tmp->sym;
			avl_insert(&ret, filtered_sm);
			if (out_of_memory())
				return NULL;

		}
	} END_FOR_EACH_SM(tmp);
	return ret;
}

static void separate_and_filter(struct sm_state *sm, int comparison, struct range_list *rl,
		struct stree *pre_stree,
		struct stree **true_states,
		struct stree **false_states)
{
	struct stree_stack *true_stack = NULL;
	struct stree_stack *false_stack = NULL;
	struct timeval time_before;
	struct timeval time_after;

	gettimeofday(&time_before, NULL);

	if (!is_merged(sm)) {
		DIMPLIED("%d '%s' is not merged.\n", get_lineno(), sm->name);
		return;
	}

	if (option_debug_implied || option_debug) {
		sm_msg("checking implications: (%s %s %s)",
		       sm->name, show_special(comparison), show_rl(rl));
	}

	separate_pools(sm, comparison, rl, &true_stack, &false_stack, NULL);

	DIMPLIED("filtering true stack.\n");
	*true_states = filter_stack(sm, pre_stree, false_stack, true_stack);
	DIMPLIED("filtering false stack.\n");
	*false_states = filter_stack(sm, pre_stree, true_stack, false_stack);
	free_stree_stack(&true_stack);
	free_stree_stack(&false_stack);
	if (option_debug_implied || option_debug) {
		printf("These are the implied states for the true path:\n");
		__print_stree(*true_states);
		printf("These are the implied states for the false path:\n");
		__print_stree(*false_states);
	}

	gettimeofday(&time_after, NULL);
	if (time_after.tv_sec - time_before.tv_sec > 7)
		__bail_on_rest_of_function = 1;
}

static struct expression *get_left_most_expr(struct expression *expr)
{
	expr = strip_expr(expr);
	if (expr->type == EXPR_ASSIGNMENT)
		return get_left_most_expr(expr->left);
	return expr;
}

static int is_merged_expr(struct expression  *expr)
{
	struct sm_state *sm;
	sval_t dummy;

	if (get_value(expr, &dummy))
		return 0;
	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;
	if (is_merged(sm))
		return 1;
	return 0;
}

static void delete_equiv_stree(struct stree **stree, const char *name, struct symbol *sym)
{
	struct smatch_state *state;
	struct relation *rel;

	state = get_state(SMATCH_EXTRA, name, sym);
	if (!estate_related(state)) {
		delete_state_stree(stree, SMATCH_EXTRA, name, sym);
		return;
	}

	FOR_EACH_PTR(estate_related(state), rel) {
		delete_state_stree(stree, SMATCH_EXTRA, rel->name, rel->sym);
	} END_FOR_EACH_PTR(rel);
}

static void handle_comparison(struct expression *expr,
			      struct stree **implied_true,
			      struct stree **implied_false)
{
	struct sm_state *sm = NULL;
	struct range_list *rl = NULL;
	struct expression *left;
	struct expression *right;
	struct symbol *type;
	int comparison = expr->op;

	left = get_left_most_expr(expr->left);
	right = get_left_most_expr(expr->right);

	if (is_merged_expr(left)) {
		sm = get_sm_state_expr(SMATCH_EXTRA, left);
		get_implied_rl(right, &rl);
	} else if (is_merged_expr(right)) {
		sm = get_sm_state_expr(SMATCH_EXTRA, right);
		get_implied_rl(left, &rl);
		comparison = flip_comparison(comparison);
	}

	if (!rl || !sm) {
		free_rl(&rl);
		return;
	}

	type = estate_type(sm->state);
	if (type_positive_bits(rl_type(rl)) > type_positive_bits(type))
		type = rl_type(rl);
	if (type_positive_bits(type) < 31)
		type = &int_ctype;
	rl = cast_rl(type, rl);

	separate_and_filter(sm, comparison, rl, __get_cur_stree(), implied_true, implied_false);
	free_rl(&rl);
	delete_equiv_stree(implied_true, sm->name, sm->sym);
	delete_equiv_stree(implied_false, sm->name, sm->sym);
}

static void handle_zero_comparison(struct expression *expr,
				struct stree **implied_true,
				struct stree **implied_false)
{
	struct symbol *sym;
	char *name;
	struct sm_state *sm;

	if (expr->type == EXPR_POSTOP)
		expr = strip_expr(expr->unop);

	if (expr->type == EXPR_ASSIGNMENT) {
		/* most of the time ->pools will be empty here because we
		   just set the state, but if have assigned a conditional
		   function there are implications. */
		expr = expr->left;
	}

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!sm)
		goto free;

	separate_and_filter(sm, SPECIAL_NOTEQUAL, tmp_range_list(estate_type(sm->state), 0), __get_cur_stree(), implied_true, implied_false);
	delete_equiv_stree(implied_true, name, sym);
	delete_equiv_stree(implied_false, name, sym);
free:
	free_string(name);
}

static int handled_by_implied_hook(struct expression *expr,
				   struct stree **implied_true,
				   struct stree **implied_false)
{
	struct stree_stack *true_stack = NULL;
	struct stree_stack *false_stack = NULL;
	struct stree *pre_stree;
	struct sm_state *sm;

	sm = comparison_implication_hook(expr, &true_stack, &false_stack);
	if (!sm)
		sm = stored_condition_implication_hook(expr, &true_stack, &false_stack);
	if (!sm)
		return 0;

	pre_stree = clone_stree(__get_cur_stree());

	*implied_true = filter_stack(sm, pre_stree, false_stack, true_stack);
	*implied_false = filter_stack(sm, pre_stree, true_stack, false_stack);

	free_stree(&pre_stree);
	free_stree_stack(&true_stack);
	free_stree_stack(&false_stack);

	return 1;
}

static void get_tf_states(struct expression *expr,
			  struct stree **implied_true,
			  struct stree **implied_false)
{
	if (handled_by_implied_hook(expr, implied_true, implied_false))
		return;

	if (expr->type == EXPR_COMPARE)
		handle_comparison(expr, implied_true, implied_false);
	else
		handle_zero_comparison(expr, implied_true, implied_false);
}

static struct stree *saved_implied_true;
static struct stree *saved_implied_false;

static void save_implications_hook(struct expression *expr)
{
	get_tf_states(expr, &saved_implied_true, &saved_implied_false);
}

static void set_implied_states(struct expression *expr)
{
	struct sm_state *sm;

	FOR_EACH_SM(saved_implied_true, sm) {
		if (!get_state_stree(saved_implied_false, sm->owner, sm->name, sm->sym)) {
			struct sm_state *orig;

			orig = get_sm_state(sm->owner, sm->name, sm->sym);
			set_state_stree(&saved_implied_false, sm->owner, sm->name, sm->sym, orig->state);
		}
	} END_FOR_EACH_SM(sm);

	FOR_EACH_SM(saved_implied_false, sm) {
		if (!get_state_stree(saved_implied_true, sm->owner, sm->name, sm->sym)) {
			struct sm_state *orig;

			orig = get_sm_state(sm->owner, sm->name, sm->sym);
			set_state_stree(&saved_implied_true, sm->owner, sm->name, sm->sym, orig->state);
		}
	} END_FOR_EACH_SM(sm);

	FOR_EACH_SM(saved_implied_true, sm) {
		__set_true_false_sm(sm, NULL);
	} END_FOR_EACH_SM(sm);
	free_stree(&saved_implied_true);

	FOR_EACH_SM(saved_implied_false, sm) {
		__set_true_false_sm(NULL, sm);
	} END_FOR_EACH_SM(sm);
	free_stree(&saved_implied_false);
}

void param_limit_implications(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	struct symbol *compare_type;
	char *name;
	struct symbol *sym;
	struct sm_state *sm;
	struct sm_state *tmp;
	struct stree *implied_true = NULL;
	struct stree *implied_false = NULL;
	struct range_list *orig, *limit, *rl;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!sm || !sm->merged)
		goto free;

	if (strcmp(key, "$") == 0)
		compare_type = get_arg_type(expr->fn, param);
	else
		compare_type = get_member_type_from_key(arg, key);

	orig = estate_rl(sm->state);
	orig = cast_rl(compare_type, orig);

	call_results_to_rl(expr, compare_type, value, &limit);
	rl = rl_intersection(orig, limit);

	separate_and_filter(sm, SPECIAL_EQUAL, rl, __get_cur_stree(), &implied_true, &implied_false);

	FOR_EACH_SM(implied_true, tmp) {
		__set_sm_cur_stree(tmp);
	} END_FOR_EACH_SM(tmp);


	free_stree(&implied_true);
	free_stree(&implied_false);
free:
	free_string(name);
}

struct range_list *__get_implied_values(struct expression *switch_expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	struct range_list *ret = NULL;

	name = expr_to_var_sym(switch_expr, &sym);
	if (!name || !sym)
		goto free;
	state = get_state(SMATCH_EXTRA, name, sym);
	if (!state)
		goto free;
	ret = clone_rl(estate_rl(state));
free:
	free_string(name);
	if (!ret) {
		struct symbol *type;

		type = get_type(switch_expr);
		ret = alloc_rl(sval_type_min(type), sval_type_max(type));
	}
	return ret;
}

struct stree *__implied_case_stree(struct expression *switch_expr,
					struct expression *case_expr,
					struct expression *case_to,
					struct range_list_stack **remaining_cases,
					struct stree **raw_stree)
{
	char *name = NULL;
	struct symbol *sym;
	struct sm_state *sm;
	struct stree *true_states = NULL;
	struct stree *false_states = NULL;
	struct stree *extra_states = NULL;
	struct stree *ret = clone_stree(*raw_stree);
	sval_t start, end;
	struct range_list *rl = NULL;

	name = expr_to_var_sym(switch_expr, &sym);
	if (!name || !sym)
		goto free;
	sm = get_sm_state_stree(*raw_stree, SMATCH_EXTRA, name, sym);

	if (get_value(case_to, &end) && get_value(case_expr, &start)) {
		filter_top_rl(remaining_cases, start, end);
		add_range(&rl, start, end);
	} else if (get_value(case_expr, &start)) {
		filter_top_rl(remaining_cases, start, start);
		add_range(&rl, start, start);
	} else {
		rl = clone_rl(top_rl(*remaining_cases));
	}

	if (sm)
		separate_and_filter(sm, SPECIAL_EQUAL, rl, *raw_stree, &true_states, &false_states);

	__push_fake_cur_stree();
	__unnullify_path();
	set_extra_nomod(name, sym, alloc_estate_rl(rl));
	extra_states = __pop_fake_cur_stree();
	overwrite_stree(extra_states, &true_states);
	overwrite_stree(true_states, &ret);
	free_stree(&extra_states);
	free_stree(&true_states);
	free_stree(&false_states);
free:
	free_string(name);
	return ret;
}

static void match_end_func(struct symbol *sym)
{
	if (__inline_fn)
		return;
	implied_debug_msg = NULL;
}

static int sm_state_in_slist(struct sm_state *sm, struct state_list *slist)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp == sm)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

/*
 * The situation is we have a SMATCH_EXTRA state and we want to break it into
 * each of the ->possible states and find the implications of each.  The caller
 * has to use __push_fake_cur_stree() to preserve the correct states so they
 * can be restored later.
 */
void overwrite_states_using_pool(struct sm_state *sm)
{
	struct sm_state *old;
	struct sm_state *new;

	if (!sm->pool)
		return;

	FOR_EACH_SM(sm->pool, old) {
		new = get_sm_state(old->owner, old->name, old->sym);
		if (!new)  /* the variable went out of scope */
			continue;
		if (sm_state_in_slist(old, new->possible))
			set_state(old->owner, old->name, old->sym, old->state);
	} END_FOR_EACH_SM(old);
}

void __extra_match_condition(struct expression *expr);
void __comparison_match_condition(struct expression *expr);
void __stored_condition(struct expression *expr);
void register_implications(int id)
{
	add_hook(&save_implications_hook, CONDITION_HOOK);
	add_hook(&set_implied_states, CONDITION_HOOK);
	add_hook(&__extra_match_condition, CONDITION_HOOK);
	add_hook(&__comparison_match_condition, CONDITION_HOOK);
	add_hook(&__stored_condition, CONDITION_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
