/*
 * Copyright (C) 2006 Dan Carpenter.
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
 * You have a lists of states.  kernel = locked, foo = NULL, ...
 * When you hit an if {} else {} statement then you swap the list
 * of states for a different list of states.  The lists are stored
 * on stacks.
 *
 * At the beginning of this file there are list of the stacks that
 * we use.  Each function in this file does something to one of
 * of the stacks.
 *
 * So the smatch_flow.c understands code but it doesn't understand states.
 * smatch_flow calls functions in this file.  This file calls functions
 * in smatch_slist.c which just has boring generic plumbing for handling
 * state lists.  But really it's this file where all the magic happens.
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

struct smatch_state undefined = { .name = "undefined" };
struct smatch_state merged = { .name = "merged" };
struct smatch_state true_state = { .name = "true" };
struct smatch_state false_state = { .name = "false" };

static struct AVL *cur_stree; /* current states */

static struct stree_stack *true_stack; /* states after a t/f branch */
static struct stree_stack *false_stack;
static struct stree_stack *pre_cond_stack; /* states before a t/f branch */

static struct stree_stack *cond_true_stack; /* states affected by a branch */
static struct stree_stack *cond_false_stack;

static struct stree_stack *fake_cur_stree_stack;
static int read_only;

static struct stree_stack *break_stack;
static struct stree_stack *switch_stack;
static struct range_list_stack *remaining_cases;
static struct stree_stack *default_stack;
static struct stree_stack *continue_stack;

static struct named_stree_stack *goto_stack;

static struct ptr_list *backup;

struct state_list_stack *implied_pools;

int option_debug;

void __print_cur_slist(void)
{
	__print_stree(cur_stree);
}

static int in_declarations_bit(void)
{
	struct statement *tmp;

	FOR_EACH_PTR_REVERSE(big_statement_stack, tmp) {
		if (tmp->type == STMT_DECLARATION)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

int unreachable(void)
{
	static int reset_warnings = 1;

	if (cur_stree) {
		if (!__inline_fn)
			reset_warnings = 1;
		return 0;
	}

	if (in_declarations_bit())
		return 1;

	/* option spammy turns on a noisier version of this */
	if (reset_warnings && !option_spammy)
		sm_msg("info: ignoring unreachable code.");
	if (!__inline_fn)
		reset_warnings = 0;
	return 1;
}

struct sm_state *set_state(int owner, const char *name, struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *ret;

	if (!name)
		return NULL;

	if (read_only)
		sm_msg("Smatch Internal Error: cur_stree is read only.");

	if (option_debug || strcmp(check_name(owner), option_debug_check) == 0) {
		struct smatch_state *s;

		s = get_state(owner, name, sym);
		if (!s)
			printf("%d new state. name='%s' [%s] %s\n",
				get_lineno(), name, check_name(owner), show_state(state));
		else
			printf("%d state change name='%s' [%s] %s => %s\n",
				get_lineno(), name, check_name(owner), show_state(s),
				show_state(state));
	}

	if (owner != -1 && unreachable())
		return NULL;

	if (fake_cur_stree_stack)
		set_state_stree_stack(&fake_cur_stree_stack, owner, name, sym, state);

	ret =  set_state_stree(&cur_stree, owner, name, sym, state);

	if (cond_true_stack) {
		set_state_stree_stack(&cond_true_stack, owner, name, sym, state);
		set_state_stree_stack(&cond_false_stack, owner, name, sym, state);
	}
	return ret;
}

struct sm_state *set_state_expr(int owner, struct expression *expr, struct smatch_state *state)
{
	char *name;
	struct symbol *sym;
	struct sm_state *ret = NULL;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = set_state(owner, name, sym, state);
free:
	free_string(name);
	return ret;
}

void __push_fake_cur_slist()
{
	push_stree(&fake_cur_stree_stack, NULL);
	__save_pre_cond_states();
}

struct AVL *__pop_fake_cur_slist()
{
	__use_pre_cond_states();
	return pop_stree(&fake_cur_stree_stack);
}

void __free_fake_cur_slist()
{
	struct AVL *stree;

	__use_pre_cond_states();
	stree = pop_stree(&fake_cur_stree_stack);
	free_stree(&stree);
}

void __set_fake_cur_slist_fast(struct state_list *slist)
{
	push_stree(&pre_cond_stack, cur_stree);
	cur_stree = slist_to_stree(slist);
	read_only = 1;
}

void __set_fake_cur_stree_fast(struct AVL *stree)
{
	push_stree(&pre_cond_stack, cur_stree);
	cur_stree = stree;
	read_only = 1;
}

void __pop_fake_cur_slist_fast()
{
	cur_stree = pop_stree(&pre_cond_stack);
	read_only = 0;
}

void __pop_fake_cur_stree_fast()
{
	cur_stree = pop_stree(&pre_cond_stack);
	read_only = 0;
}

void __merge_slist_into_cur(struct state_list *slist)
{
	struct sm_state *sm;
	struct sm_state *orig;
	struct sm_state *merged;

	FOR_EACH_PTR(slist, sm) {
		orig = get_sm_state(sm->owner, sm->name, sm->sym);
		if (orig)
			merged = merge_sm_states(orig, sm);
		else
			merged = sm;
		__set_sm(merged);
	} END_FOR_EACH_PTR(sm);
}

void __set_sm(struct sm_state *sm)
{
	if (read_only)
		sm_msg("Smatch Internal Error: cur_stree is read only.");

	if (option_debug ||
	    strcmp(check_name(sm->owner), option_debug_check) == 0) {
		struct smatch_state *s;

		s = get_state(sm->owner, sm->name, sm->sym);
		if (!s)
			printf("%d new state. name='%s' [%s] %s\n",
				get_lineno(), sm->name, check_name(sm->owner),
				show_state(sm->state));
		else
			printf("%d state change name='%s' [%s] %s => %s\n",
				get_lineno(), sm->name, check_name(sm->owner), show_state(s),
				show_state(sm->state));
	}

	if (unreachable())
		return;

	if (fake_cur_stree_stack)
		overwrite_sm_state_stree_stack(&fake_cur_stree_stack, sm);

	overwrite_sm_state_stree(&cur_stree, sm);

	if (cond_true_stack) {
		overwrite_sm_state_stree_stack(&cond_true_stack, sm);
		overwrite_sm_state_stree_stack(&cond_false_stack, sm);
	}
}

struct smatch_state *get_state(int owner, const char *name, struct symbol *sym)
{
	return get_state_stree(cur_stree, owner, name, sym);
}

struct smatch_state *get_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *ret = NULL;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_state(owner, name, sym);
free:
	free_string(name);
	return ret;
}

struct state_list *get_possible_states(int owner, const char *name, struct symbol *sym)
{
	struct sm_state *sms;

	sms = get_sm_state_stree(cur_stree, owner, name, sym);
	if (sms)
		return sms->possible;
	return NULL;
}

struct state_list *get_possible_states_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct state_list *ret = NULL;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_possible_states(owner, name, sym);
free:
	free_string(name);
	return ret;
}

struct sm_state *get_sm_state(int owner, const char *name, struct symbol *sym)
{
	return get_sm_state_stree(cur_stree, owner, name, sym);
}

struct sm_state *get_sm_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct sm_state *ret = NULL;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_sm_state(owner, name, sym);
free:
	free_string(name);
	return ret;
}

void delete_state(int owner, const char *name, struct symbol *sym)
{
	delete_state_stree(&cur_stree, owner, name, sym);
	if (cond_true_stack) {
		delete_state_stree_stack(&pre_cond_stack, owner, name, sym);
		delete_state_stree_stack(&cond_true_stack, owner, name, sym);
		delete_state_stree_stack(&cond_false_stack, owner, name, sym);
	}
}

void delete_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	delete_state(owner, name, sym);
free:
	free_string(name);
}

struct state_list *get_all_states_slist(int owner, struct state_list *source)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp;

	FOR_EACH_PTR(source, tmp) {
		if (tmp->owner == owner)
			add_ptr_list(&slist, tmp);
	} END_FOR_EACH_PTR(tmp);

	return slist;
}

struct state_list *get_all_states(int owner)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp;

	FOR_EACH_SM(cur_stree, tmp) {
		if (tmp->owner == owner)
			add_ptr_list(&slist, tmp);
	} END_FOR_EACH_SM(tmp);

	return slist;
}

int is_reachable(void)
{
	if (cur_stree)
		return 1;
	return 0;
}

struct state_list *__get_cur_slist(void)
{
	return stree_to_slist(cur_stree);
}

void set_true_false_states(int owner, const char *name, struct symbol *sym,
			   struct smatch_state *true_state,
			   struct smatch_state *false_state)
{
	if (option_debug || strcmp(check_name(owner), option_debug_check) == 0) {
		struct smatch_state *tmp;

		tmp = get_state(owner, name, sym);
		printf("%d set_true_false '%s'.  Was %s.  Now T:%s F:%s\n",
		       get_lineno(), name, show_state(tmp),
		       show_state(true_state), show_state(false_state));
	}

	if (unreachable())
		return;

	if (!cond_false_stack || !cond_true_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}

	if (true_state) {
		set_state_stree(&cur_stree, owner, name, sym, true_state);
		set_state_stree_stack(&cond_true_stack, owner, name, sym, true_state);
	}
	if (false_state)
		set_state_stree_stack(&cond_false_stack, owner, name, sym, false_state);
}

void set_true_false_states_expr(int owner, struct expression *expr,
			   struct smatch_state *true_state,
			   struct smatch_state *false_state)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	set_true_false_states(owner, name, sym, true_state, false_state);
free:
	free_string(name);
}

void __set_true_false_sm(struct sm_state *true_sm, struct sm_state *false_sm)
{
	if (unreachable())
		return;

	if (!cond_false_stack || !cond_true_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}

	if (true_sm) {
		overwrite_sm_state_stree(&cur_stree, true_sm);
		overwrite_sm_state_stree_stack(&cond_true_stack, true_sm);
	}
	if (false_sm)
		overwrite_sm_state_stree_stack(&cond_false_stack, false_sm);
}

void nullify_path(void)
{
	free_stree(&cur_stree);
}

void __match_nullify_path_hook(const char *fn, struct expression *expr,
			       void *unused)
{
	nullify_path();
}

/*
 * At the start of every function we mark the path
 * as unnull.  That way there is always at least one state
 * in the cur_stree until nullify_path is called.  This
 * is used in merge_slist() for the first null check.
 */
void __unnullify_path(void)
{
	set_state(-1, "unnull_path", NULL, &true_state);
}

int __path_is_null(void)
{
	if (cur_stree)
		return 0;
	return 1;
}

static void check_stree_stack_free(struct stree_stack **stack)
{
	if (*stack) {
		sm_msg("smatch internal error:  stack not empty");
		free_stack_and_strees(stack);
	}
}

void save_all_states(void)
{
	__add_ptr_list(&backup, cur_stree, 0);

	__add_ptr_list(&backup, true_stack, 0);
	__add_ptr_list(&backup, false_stack, 0);
	__add_ptr_list(&backup, pre_cond_stack, 0);

	__add_ptr_list(&backup, cond_true_stack, 0);
	__add_ptr_list(&backup, cond_false_stack, 0);

	__add_ptr_list(&backup, fake_cur_stree_stack, 0);

	__add_ptr_list(&backup, break_stack, 0);
	__add_ptr_list(&backup, switch_stack, 0);
	__add_ptr_list(&backup, remaining_cases, 0);
	__add_ptr_list(&backup, default_stack, 0);
	__add_ptr_list(&backup, continue_stack, 0);

	__add_ptr_list(&backup, goto_stack, 0);
}

void nullify_all_states(void)
{
	/* FIXME:  These should call free_stree() */
	cur_stree = NULL;

	true_stack = NULL;
	false_stack = NULL;
	pre_cond_stack = NULL;

	cond_true_stack = NULL;
	cond_false_stack = NULL;

	fake_cur_stree_stack = NULL;

	break_stack = NULL;
	switch_stack = NULL;
	remaining_cases = NULL;
	default_stack = NULL;
	continue_stack = NULL;

	goto_stack = NULL;
}

static void *pop_backup(void)
{
	void *ret;

	ret = last_ptr_list(backup);
	delete_ptr_list_last(&backup);
	return ret;
}

void restore_all_states(void)
{
	goto_stack = pop_backup();

	continue_stack = pop_backup();
	default_stack = pop_backup();
	remaining_cases = pop_backup();
	switch_stack = pop_backup();
	break_stack = pop_backup();

	fake_cur_stree_stack = pop_backup();

	cond_false_stack = pop_backup();
	cond_true_stack = pop_backup();

	pre_cond_stack = pop_backup();
	false_stack = pop_backup();
	true_stack = pop_backup();

	cur_stree = pop_backup();
}

void clear_all_states(void)
{
	struct named_stree *named_stree;

	nullify_path();
	check_stree_stack_free(&true_stack);
	check_stree_stack_free(&false_stack);
	check_stree_stack_free(&pre_cond_stack);
	check_stree_stack_free(&cond_true_stack);
	check_stree_stack_free(&cond_false_stack);
	check_stree_stack_free(&break_stack);
	check_stree_stack_free(&switch_stack);
	check_stree_stack_free(&continue_stack);
	free_stack_and_slists(&implied_pools);

	FOR_EACH_PTR(goto_stack, named_stree) {
		free_stree(&named_stree->stree);
	} END_FOR_EACH_PTR(named_stree);
	__free_ptr_list((struct ptr_list **)&goto_stack);
	free_every_single_sm_state();
}

void __push_cond_stacks(void)
{
	push_stree(&cond_true_stack, NULL);
	push_stree(&cond_false_stack, NULL);
}

struct AVL *__copy_cond_true_states(void)
{
	struct AVL *ret;

	ret = pop_stree(&cond_true_stack);
	push_stree(&cond_true_stack, clone_stree(ret));
	return ret;
}

struct AVL *__copy_cond_false_states(void)
{
	struct AVL *ret;

	ret = pop_stree(&cond_false_stack);
	push_stree(&cond_false_stack, clone_stree(ret));
	return ret;
}

struct AVL *__pop_cond_true_stack(void)
{
	return pop_stree(&cond_true_stack);
}

struct AVL *__pop_cond_false_stack(void)
{
	return pop_stree(&cond_false_stack);
}

/*
 * This combines the pre cond states with either the true or false states.
 * For example:
 * a = kmalloc() ; if (a !! foo(a)
 * In the pre state a is possibly null.  In the true state it is non null.
 * In the false state it is null.  Combine the pre and the false to get
 * that when we call 'foo', 'a' is null.
 */
static void __use_cond_stack(struct stree_stack **stack)
{
	struct AVL *stree;

	free_stree(&cur_stree);

	cur_stree = pop_stree(&pre_cond_stack);
	push_stree(&pre_cond_stack, clone_stree(cur_stree));

	stree = pop_stree(stack);
	overwrite_stree(stree, &cur_stree);
	push_stree(stack, stree);
}

void __use_pre_cond_states(void)
{
	free_stree(&cur_stree);
	cur_stree = pop_stree(&pre_cond_stack);
}

void __use_cond_true_states(void)
{
	__use_cond_stack(&cond_true_stack);
}

void __use_cond_false_states(void)
{
	__use_cond_stack(&cond_false_stack);
}

void __negate_cond_stacks(void)
{
	struct AVL *old_false, *old_true;

	__use_cond_stack(&cond_false_stack);
	old_false = pop_stree(&cond_false_stack);
	old_true = pop_stree(&cond_true_stack);
	push_stree(&cond_false_stack, old_true);
	push_stree(&cond_true_stack, old_false);
}

void __and_cond_states(void)
{
	and_stree_stack(&cond_true_stack);
	or_stree_stack(&pre_cond_stack, cur_stree, &cond_false_stack);
}

void __or_cond_states(void)
{
	or_stree_stack(&pre_cond_stack, cur_stree, &cond_true_stack);
	and_stree_stack(&cond_false_stack);
}

void __save_pre_cond_states(void)
{
	push_stree(&pre_cond_stack, clone_stree(cur_stree));
}

void __discard_pre_cond_states(void)
{
	struct AVL *tmp;

	tmp = pop_stree(&pre_cond_stack);
	free_stree(&tmp);
}

void __use_cond_states(void)
{
	struct AVL *pre, *pre_clone, *true_states, *false_states;

	pre = pop_stree(&pre_cond_stack);
	pre_clone = clone_stree(pre);

	true_states = pop_stree(&cond_true_stack);
	overwrite_stree(true_states, &pre);
	/* we use the true states right away */
	free_stree(&cur_stree);
	cur_stree = pre;

	false_states = pop_stree(&cond_false_stack);
	overwrite_stree(false_states, &pre_clone);
	push_stree(&false_stack, pre_clone);
}

void __push_true_states(void)
{
	push_stree(&true_stack, clone_stree(cur_stree));
}

void __use_false_states(void)
{
	free_stree(&cur_stree);
	cur_stree = pop_stree(&false_stack);
}

void __discard_false_states(void)
{
	struct AVL *stree;

	stree = pop_stree(&false_stack);
	free_stree(&stree);
}

void __merge_false_states(void)
{
	struct AVL *stree;

	stree = pop_stree(&false_stack);
	merge_stree(&cur_stree, stree);
	free_stree(&stree);
}

void __merge_true_states(void)
{
	struct AVL *stree;

	stree = pop_stree(&true_stack);
	merge_stree(&cur_stree, stree);
	free_stree(&stree);
}

void __push_continues(void)
{
	push_stree(&continue_stack, NULL);
}

void __discard_continues(void)
{
	struct AVL *stree;

	stree = pop_stree(&continue_stack);
	free_stree(&stree);
}

void __process_continues(void)
{
	struct AVL *stree;

	stree = pop_stree(&continue_stack);
	if (!stree)
		stree = clone_stree(cur_stree);
	else
		merge_stree(&stree, cur_stree);

	push_stree(&continue_stack, stree);
}

static int top_stree_empty(struct stree_stack **stack)
{
	struct AVL *tmp;
	int empty = 0;

	tmp = pop_stree(stack);
	if (!tmp)
		empty = 1;
	push_stree(stack, tmp);
	return empty;
}

/* a silly loop does this:  while(i--) { return; } */
void __warn_on_silly_pre_loops(void)
{
	if (!__path_is_null())
		return;
	if (!top_stree_empty(&continue_stack))
		return;
	if (!top_stree_empty(&break_stack))
		return;
	/* if the path was nullified before the loop, then we already
	   printed an error earlier */
	if (top_stree_empty(&false_stack))
		return;
	sm_msg("info: loop could be replaced with if statement.");
}

void __merge_continues(void)
{
	struct AVL *stree;

	stree = pop_stree(&continue_stack);
	merge_stree(&cur_stree, stree);
	free_stree(&stree);
}

void __push_breaks(void)
{
	push_stree(&break_stack, NULL);
}

void __process_breaks(void)
{
	struct AVL *stree;

	stree = pop_stree(&break_stack);
	if (!stree)
		stree = clone_stree(cur_stree);
	else
		merge_stree(&stree, cur_stree);

	push_stree(&break_stack, stree);
}

int __has_breaks(void)
{
	struct AVL *stree;
	int ret;

	stree = pop_stree(&break_stack);
	ret = !!stree;
	push_stree(&break_stack, stree);
	return ret;
}

void __merge_breaks(void)
{
	struct AVL *stree;

	stree = pop_stree(&break_stack);
	merge_stree(&cur_stree, stree);
	free_stree(&stree);
}

void __use_breaks(void)
{
	free_stree(&cur_stree);
	cur_stree = pop_stree(&break_stack);
}

void __save_switch_states(struct expression *switch_expr)
{
	push_rl(&remaining_cases, __get_implied_values(switch_expr));
	push_stree(&switch_stack, clone_stree(cur_stree));
}

void __merge_switches(struct expression *switch_expr, struct expression *case_expr)
{
	struct AVL *stree;
	struct state_list *implied_slist;

	stree = pop_stree(&switch_stack);
	implied_slist = __implied_case_slist(switch_expr, case_expr, &remaining_cases, &stree);
	merge_stree(&cur_stree, slist_to_stree(implied_slist));
	free_slist(&implied_slist);
	push_stree(&switch_stack, stree);
}

void __discard_switches(void)
{
	struct AVL *stree;

	pop_rl(&remaining_cases);
	stree = pop_stree(&switch_stack);
	free_stree(&stree);
}

void __push_default(void)
{
	push_stree(&default_stack, NULL);
}

void __set_default(void)
{
	set_state_stree_stack(&default_stack, 0, "has_default", NULL, &true_state);
}

int __pop_default(void)
{
	struct AVL *stree;

	stree = pop_stree(&default_stack);
	if (stree) {
		free_stree(&stree);
		return 1;
	}
	return 0;
}

static struct named_stree *alloc_named_stree(const char *name, struct AVL *stree)
{
	struct named_stree *named_stree = __alloc_named_stree(0);

	named_stree->name = (char *)name;
	named_stree->stree = stree;
	return named_stree;
}

void __save_gotos(const char *name)
{
	struct AVL **stree;
	struct AVL *clone;

	stree = get_named_stree(goto_stack, name);
	if (stree) {
		merge_stree(stree, cur_stree);
		return;
	} else {
		struct named_stree *named_stree;

		clone = clone_stree(cur_stree);
		named_stree = alloc_named_stree(name, clone);
		add_ptr_list(&goto_stack, named_stree);
	}
}

void __merge_gotos(const char *name)
{
	struct AVL **stree;

	stree = get_named_stree(goto_stack, name);
	if (stree)
		merge_stree(&cur_stree, *stree);
}
