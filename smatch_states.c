/*
 * sparse/smatch_states.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
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

static struct state_list *cur_slist; /* current states */

static struct state_list_stack *true_stack; /* states after a t/f branch */
static struct state_list_stack *false_stack;
static struct state_list_stack *pre_cond_stack; /* states before a t/f branch */

static struct state_list_stack *cond_true_stack; /* states affected by a branch */
static struct state_list_stack *cond_false_stack;

int __fake_cur = 0;
struct state_list *__fake_cur_slist = NULL;
int __fake_conditions = 0;
struct state_list *__fake_cond_true = NULL;
struct state_list *__fake_cond_false = NULL;

static struct state_list_stack *break_stack;
static struct state_list_stack *switch_stack;
static struct range_list_stack *remaining_cases;
static struct state_list_stack *default_stack;
static struct state_list_stack *continue_stack;

static struct named_stack *goto_stack;

struct state_list_stack *implied_pools;

int option_debug;

void __print_cur_slist()
{
	__print_slist(cur_slist);
}

int unreachable()
{
	static int reset_warnings = 1;

	if (cur_slist) {
		reset_warnings = 1;
		return 0;
	}

	if (reset_warnings || option_debug) 
		sm_msg("info: ignoring unreachable code.");
	reset_warnings = 0;
	return 1;
}

void set_state(int owner, const char *name, struct symbol *sym, 
	       struct smatch_state *state)
{
	if (!name)
		return;
	
	if (option_debug) {
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
		return;
	
	if (__fake_cur) {
		set_state_slist(&__fake_cur_slist, owner, name, sym, state);
		return;
	}

	set_state_slist(&cur_slist, owner, name, sym, state);

	if (cond_true_stack) {
		set_state_stack(&cond_true_stack, owner, name, sym, state);
		set_state_stack(&cond_false_stack, owner, name, sym, state);
	}
}

void set_state_expr(int owner, struct expression *expr,
	       struct smatch_state *state)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	reset_on_container_modified(owner, expr);
	set_state(owner, name, sym, state);
free:
	free_string(name);

}

void __set_state(struct sm_state *sm)
{
	if (option_debug) {
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

	overwrite_sm_state(&cur_slist, sm);

	if (cond_true_stack) {
		overwrite_sm_state_stack(&cond_true_stack, sm);
		overwrite_sm_state_stack(&cond_false_stack, sm);
	}
}

struct smatch_state *get_state(int owner, const char *name, struct symbol *sym)
{
	return get_state_slist(cur_slist, owner, name, sym);
}

struct smatch_state *get_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *ret = NULL;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_state(owner, name, sym);
free:
	free_string(name);
	return ret;
}

struct state_list *get_possible_states(int owner, const char *name, 
				       struct symbol *sym)
{
	struct sm_state *sms;

	sms = get_sm_state_slist(cur_slist, owner, name, sym);
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
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_possible_states(owner, name, sym);
free:
	free_string(name);
	return ret;
}

struct sm_state *get_sm_state(int owner, const char *name, struct symbol *sym)
{
	return get_sm_state_slist(cur_slist, owner, name, sym);
}

struct sm_state *get_sm_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct sm_state *ret = NULL;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = get_sm_state(owner, name, sym);
free:
	free_string(name);
	return ret;
}

void delete_state(int owner, const char *name, struct symbol *sym)
{
	delete_state_slist(&cur_slist, owner, name, sym);
}

void delete_state_expr(int owner, struct expression *expr)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	delete_state(owner, name, sym);
free:
	free_string(name);
}

struct state_list *get_all_states(int owner)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp;

	FOR_EACH_PTR(cur_slist, tmp) {
		if (tmp->owner == owner) {
			add_ptr_list(&slist, tmp);
		}
	} END_FOR_EACH_PTR(tmp);

	return slist;
}

int is_reachable()
{
	if (cur_slist)
		return 1;
	return 0;
}

struct state_list *__get_cur_slist()
{
	return cur_slist;
}

void set_true_false_states(int owner, const char *name, struct symbol *sym, 
			   struct smatch_state *true_state,
			   struct smatch_state *false_state)
{
	if (option_debug) {
		struct smatch_state *tmp;

		tmp = get_state(owner, name, sym);
		sm_debug("%d set_true_false '%s'.  Was %s.  Now T:%s F:%s\n",
			 get_lineno(), name, show_state(tmp),
			 show_state(true_state), show_state(false_state));
	}

	if (unreachable())
		return;

	if (__fake_conditions) {
		if (true_state)
			set_state_slist(&__fake_cond_true, owner, name, sym, true_state);
		if (false_state)
			set_state_slist(&__fake_cond_false, owner, name, sym, false_state);
		return;
	}

	if (!cond_false_stack || !cond_true_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}

	if (true_state) {
		set_state_slist(&cur_slist, owner, name, sym, true_state);
		set_state_stack(&cond_true_stack, owner, name, sym, true_state);
	}
	if (false_state)
		set_state_stack(&cond_false_stack, owner, name, sym, false_state);
}

void set_true_false_states_expr(int owner, struct expression *expr, 
			   struct smatch_state *true_state,
			   struct smatch_state *false_state)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	reset_on_container_modified(owner, expr);
	set_true_false_states(owner, name, sym, true_state, false_state);
free:
	free_string(name);
}

void __set_true_false_sm(struct sm_state *true_state, 
			struct sm_state *false_state)
{
	if (unreachable())
		return;

	if (!cond_false_stack || !cond_true_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}

	if (true_state) {
		overwrite_sm_state(&cur_slist, true_state);
		overwrite_sm_state_stack(&cond_true_stack, true_state);
	}
	if (false_state)
		overwrite_sm_state_stack(&cond_false_stack, false_state);
}

void nullify_path()
{
	free_slist(&cur_slist);
}

void __match_nullify_path_hook(const char *fn, struct expression *expr,
			       void *unused)
{
	nullify_path();
}
/*
 * At the start of every function we mark the path
 * as unnull.  That there is always at least one state
 * in the cur_slist until nullify_path is called.  This
 * is used in merge_slist() for the first null check.
 */

void __unnullify_path()
{
	set_state(-1, "unnull_path", NULL, &true_state);
}

int __path_is_null()
{
	if (cur_slist)
		return 0;
	return 1;
}

void clear_all_states()
{
	struct named_slist *named_slist;

	nullify_path();
	free_stack_and_slists(&true_stack);
	free_stack_and_slists(&false_stack);
	free_stack_and_slists(&pre_cond_stack);
	free_stack_and_slists(&cond_true_stack);
	free_stack_and_slists(&cond_false_stack);
	free_stack_and_slists(&break_stack);
	free_stack_and_slists(&switch_stack);
	free_stack_and_slists(&continue_stack);
	free_stack_and_slists(&implied_pools);

	FOR_EACH_PTR(goto_stack, named_slist) {
		free_slist(&named_slist->slist);
	} END_FOR_EACH_PTR(named_slist);
	__free_ptr_list((struct ptr_list **)&goto_stack);
	free_every_single_sm_state();
}


void __push_cond_stacks()
{
	push_slist(&cond_true_stack, NULL);
	push_slist(&cond_false_stack, NULL);
}

/*
 * This combines the pre cond states with either the true or false states.
 * For example:
 * a = kmalloc() ; if (a !! foo(a)
 * In the pre state a is possibly null.  In the true state it is non null.
 * In the false state it is null.  Combine the pre and the false to get
 * that when we call 'foo', 'a' is null.
 */

static void __use_cond_stack(struct state_list_stack **stack)
{
	struct state_list *slist;
	
	free_slist(&cur_slist);

	cur_slist = pop_slist(&pre_cond_stack);
	push_slist(&pre_cond_stack, clone_slist(cur_slist));

	slist = pop_slist(stack);
	overwrite_slist(slist, &cur_slist);
	push_slist(stack, slist);
}

void __save_false_states_for_later()
{
	struct state_list *pre_conditions;
	struct state_list *false_states;
	struct state_list *tmp;

	pre_conditions = pop_slist(&pre_cond_stack);
	false_states = pop_slist(&cond_false_stack);
	tmp = clone_slist(pre_conditions);

	overwrite_slist(false_states, &tmp);

	push_slist(&pre_cond_stack, tmp);
	push_slist(&pre_cond_stack, pre_conditions);
	push_slist(&cond_false_stack, false_states);
}

void __use_previously_stored_false_states()
{
	free_slist(&cur_slist);
	cur_slist = pop_slist(&pre_cond_stack);
}

void __use_cond_true_states()
{
	__use_cond_stack(&cond_true_stack);
}

void __use_cond_false_states()
{
	__use_cond_stack(&cond_false_stack);
}

void __negate_cond_stacks()
{
	struct state_list *old_false, *old_true;
	
 	__use_cond_stack(&cond_false_stack);
	old_false = pop_slist(&cond_false_stack);
	old_true = pop_slist(&cond_true_stack);
	push_slist(&cond_false_stack, old_true);
	push_slist(&cond_true_stack, old_false);
}

void __and_cond_states()
{
	and_slist_stack(&cond_true_stack);
	or_slist_stack(&pre_cond_stack, cur_slist, &cond_false_stack);
}

void __or_cond_states()
{
	or_slist_stack(&pre_cond_stack, cur_slist, &cond_true_stack);
	and_slist_stack(&cond_false_stack);
}

void __save_pre_cond_states()
{
	push_slist(&pre_cond_stack, clone_slist(cur_slist));
}

void __pop_pre_cond_states()
{
	struct state_list *tmp;
	
	tmp = pop_slist(&pre_cond_stack);
	free_slist(&tmp);
}

void __use_cond_states()
{
	struct state_list *pre, *pre_clone, *true_states, *false_states;

	pre = pop_slist(&pre_cond_stack);
	pre_clone = clone_slist(pre);

	true_states = pop_slist(&cond_true_stack);
	overwrite_slist(true_states, &pre);
	/* we use the true states right away */
	free_slist(&cur_slist);
	cur_slist = pre;


	false_states = pop_slist(&cond_false_stack);	
	overwrite_slist(false_states, &pre_clone);
	push_slist(&false_stack, pre_clone);
}

void __push_true_states()
{
	push_slist(&true_stack, clone_slist(cur_slist));
}

void __use_false_states()
{
	free_slist(&cur_slist);
	cur_slist = pop_slist(&false_stack);
}

void __pop_false_states()
{
	struct state_list *slist;

	slist = pop_slist(&false_stack);
	free_slist(&slist);
}

void __merge_false_states()
{
	struct state_list *slist;

	slist = pop_slist(&false_stack);
	merge_slist(&cur_slist, slist);
	free_slist(&slist);
}

void __merge_true_states()
{
	struct state_list *slist;

	slist = pop_slist(&true_stack);
	merge_slist(&cur_slist, slist);
	free_slist(&slist);
}

void __push_continues() 
{ 
	push_slist(&continue_stack, NULL);
}

void __pop_continues() 
{ 
	struct state_list *slist;

	slist = pop_slist(&continue_stack);
	free_slist(&slist);
}

void __process_continues()
{
	struct state_list *slist;

	slist = pop_slist(&continue_stack);
	if (!slist) {
		slist = clone_slist(cur_slist);
	} else {
		merge_slist(&slist, cur_slist);
	}
	push_slist(&continue_stack, slist);
}

static int top_slist_empty(struct state_list_stack **stack)
{
	struct state_list *tmp;
	int empty = 0;

	tmp = pop_slist(stack);
	if (!tmp)
		empty = 1;
	push_slist(stack, tmp);
	return empty;
}

/* a silly loop does this:  while(i--) { return; } */
void __warn_on_silly_pre_loops()
{
	if (!__path_is_null())
		return;
	if (!top_slist_empty(&continue_stack))
		return;
	if (!top_slist_empty(&break_stack))
		return;
	/* if the path was nullified before the loop, then we already
	   printed an error earlier */
	if (top_slist_empty(&false_stack))
		return;
	sm_msg("info: loop could be replaced with if statement.");
}

void __merge_continues()
{
	struct state_list *slist;

	slist = pop_slist(&continue_stack);
	merge_slist(&cur_slist, slist);
	free_slist(&slist);
}

void __push_breaks() 
{ 
	push_slist(&break_stack, NULL);
}

void __process_breaks()
{
	struct state_list *slist;
	
	slist = pop_slist(&break_stack);
	if (!slist) {
		slist = clone_slist(cur_slist);
	} else {
		merge_slist(&slist, cur_slist);
	}
	push_slist(&break_stack, slist);
}

void __merge_breaks()
{
	struct state_list *slist;

	slist = pop_slist(&break_stack);
	merge_slist(&cur_slist, slist);
	free_slist(&slist);
}

void __use_breaks()
{
	free_slist(&cur_slist);
	cur_slist = pop_slist(&break_stack);
}

void __save_switch_states(struct expression *switch_expr)
{ 
	push_range_list(&remaining_cases, __get_implied_values(switch_expr));
	push_slist(&switch_stack, clone_slist(cur_slist));
}

void __merge_switches(struct expression *switch_expr, struct expression *case_expr)
{
	struct state_list *slist;
	struct state_list *implied_slist;

	slist = pop_slist(&switch_stack);
	implied_slist = __implied_case_slist(switch_expr, case_expr, &remaining_cases, &slist);
	merge_slist(&cur_slist, implied_slist);
	free_slist(&implied_slist);
	push_slist(&switch_stack, slist);
}

void __pop_switches() 
{ 
	struct state_list *slist;

	pop_range_list(&remaining_cases);
	slist = pop_slist(&switch_stack);
	free_slist(&slist);
}

void __push_default()
{
	push_slist(&default_stack, NULL);
}

void __set_default()
{
	set_state_stack(&default_stack, 0, "has_default", NULL, &true_state);
}

int __pop_default()
{
	struct state_list *slist;

	slist = pop_slist(&default_stack);
	if (slist) {
		free_slist(&slist);
		return 1;
	}
	return 0;
}

static struct named_slist *alloc_named_slist(const char *name, 
					     struct state_list *slist)
{
	struct named_slist *named_slist = __alloc_named_slist(0);

	named_slist->name = (char *)name;
	named_slist->slist = slist;
	return named_slist;
}

void __save_gotos(const char *name)
{
	struct state_list **slist;
	struct state_list *clone;

	slist = get_slist_from_named_stack(goto_stack, name);
	if (slist) {
		clone = clone_slist(cur_slist);
		merge_slist(slist, clone);
		free_slist(&clone);
		return;
	} else {
		struct named_slist *named_slist;

		clone = clone_slist(cur_slist);
		named_slist = alloc_named_slist(name, clone);
		add_ptr_list(&goto_stack, named_slist);
	}
}

void __merge_gotos(const char *name)
{
	struct state_list **slist;
	
	slist = get_slist_from_named_stack(goto_stack, name);
	if (slist)
		merge_slist(&cur_slist, *slist);
}
