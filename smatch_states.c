/*
 * sparse/smatch_states.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"

struct state_list *cur_slist; /* current states */

static struct state_list_stack *true_stack; /* states after a t/f branch */
static struct state_list_stack *false_stack;
static struct state_list_stack *pre_cond_stack; /* states before a t/f branch */

static struct state_list_stack *cond_true_stack; /* states affected by a branch */
static struct state_list_stack *cond_false_stack;

static struct state_list_stack *break_stack;
static struct state_list_stack *switch_stack;
static struct state_list_stack *default_stack;
static struct state_list_stack *continue_stack;
static struct state_list_stack *false_only_stack;

struct slist_stack *goto_stack;

int debug_states;

void __print_slist(struct state_list *slist)
{
	struct smatch_state *state;

	printf("dumping slist at %d\n", get_lineno());
	FOR_EACH_PTR(slist, state) {
		printf("%s=%d\n", state->name, state->state);
	} END_FOR_EACH_PTR(state);
	printf("---\n");
}

void __print_cur_slist()
{
	__print_slist(cur_slist);
}

void set_state(const char *name, int owner, struct symbol *sym, int state)
{
	if (!name)
		return;
	
	if (debug_states) {
		int s;
		
		s = get_state(name, owner, sym);
		if (s == NOTFOUND)
			printf("%d new state. name='%s' owner=%d: %d\n", 
			       get_lineno(), name, owner, state);
		else
			printf("%d state change name='%s' owner=%d: %d => %d\n",
			       get_lineno(), name, owner, s, state);
	}
	set_state_slist(&cur_slist, name, owner, sym, state);
}

int get_state(const char *name, int owner, struct symbol *sym)
{
	return get_state_slist(cur_slist, name, owner, sym);
}

void delete_state(const char *name, int owner, struct symbol *sym)
{
	delete_state_slist(&cur_slist, name, owner, sym);
}

struct state_list *get_current_states(int owner)
{
	struct state_list *slist;
	struct smatch_state *tmp;

	FOR_EACH_PTR(cur_slist, tmp) {
		if (tmp->owner == owner) {
			add_ptr_list(&slist, tmp);
		}
	} END_FOR_EACH_PTR(tmp);

	return slist;
}

void set_true_false_states(const char *name, int owner, struct symbol *sym, 
			   int true_state, int false_state)
{

	/* fixme.  save history */

  	SM_DEBUG("%d set_true_false %s.  Was %d.  Now T:%d F:%d\n",
		 get_lineno(), name, get_state(name, owner, sym), true_state, 
		 false_state);

	if (!cond_false_stack || !cond_true_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}

	set_state_slist(&cur_slist, name, owner, sym, true_state);
	set_state_stack(&cond_true_stack, name, owner, sym, true_state);
	set_state_stack(&cond_false_stack, name, owner, sym, false_state);

}

void nullify_path()
{
	del_slist(&cur_slist);
}

void clear_all_states()
{
	struct named_slist *named_slist;

	nullify_path();
	del_slist_stack(&true_stack);
	del_slist_stack(&false_stack);
	del_slist_stack(&false_only_stack);
	del_slist_stack(&pre_cond_stack);
	del_slist_stack(&cond_true_stack);
	del_slist_stack(&cond_false_stack);
	del_slist_stack(&break_stack);
	del_slist_stack(&switch_stack);
	del_slist_stack(&continue_stack);

	FOR_EACH_PTR(goto_stack, named_slist) {
		del_slist(&named_slist->slist);
	} END_FOR_EACH_PTR(named_slist);
	__free_ptr_list((struct ptr_list **)&goto_stack);
}


void __push_cond_stacks()
{
	push_slist(&cond_true_stack, NULL);
	push_slist(&cond_false_stack, NULL);
}

static void __use_cond_stack(struct state_list_stack **stack)
{
	struct state_list *slist;
	struct smatch_state *tmp;
	
	del_slist(&cur_slist);
	cur_slist = pop_slist(&pre_cond_stack);
	push_slist(&pre_cond_stack, clone_slist(cur_slist));

	slist = pop_slist(stack);
	FOR_EACH_PTR(slist, tmp) {
		set_state(tmp->name, tmp->owner, tmp->sym, tmp->state);
	} END_FOR_EACH_PTR(tmp);
	push_slist(stack, slist);
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
	
	old_false = pop_slist(&cond_false_stack);
	old_true = pop_slist(&cond_true_stack);

	overwrite_slist(old_false, &cur_slist);

	push_slist(&cond_false_stack, old_true);
	push_slist(&cond_true_stack, old_false);
}


void __and_cond_states()
{
	struct state_list *tmp_slist;

	tmp_slist = pop_slist(&cond_true_stack);
	and_slist_stack(&cond_true_stack, tmp_slist);
	or_slist_stack(&cond_false_stack);
}

void __or_cond_states()
{
	struct state_list *tmp_slist;

	or_slist_stack(&cond_true_stack);
	tmp_slist = pop_slist(&cond_false_stack);
	and_slist_stack(&cond_false_stack, tmp_slist);
}


void __save_pre_cond_states()
{
	push_slist(&pre_cond_stack, clone_slist(cur_slist));
}

void __pop_pre_cond_states()
{
	struct state_list *tmp;
	
	tmp = pop_slist(&pre_cond_stack);
	del_slist(&tmp);
}

void __use_false_only_stack()
{
	struct state_list *slist;

	slist = pop_slist(&false_only_stack);
	overwrite_slist(slist, &cur_slist);
	del_slist(&slist);
}

void __pop_false_only_stack()
{
	struct state_list *slist;

	slist = pop_slist(&false_only_stack);
	del_slist(&slist);
}

void __use_cond_states()
{
	struct state_list *tmp, *tmp2, *tmp3;

	tmp = pop_slist(&cond_false_stack);	
	push_slist(&false_only_stack, clone_slist(tmp));
	push_slist(&cond_false_stack, tmp);

	/* Everyone calls __use_true_states so setting up
	   the true stack is enough here */
	tmp = pop_slist(&pre_cond_stack);
	tmp3 = clone_slist(tmp);
	tmp2 = pop_slist(&cond_true_stack);
	overwrite_slist(tmp2, &tmp3);
	push_slist(&true_stack, tmp3);

	tmp2 = pop_slist(&cond_false_stack);
	overwrite_slist(tmp2, &tmp);
	push_slist(&false_stack, tmp);
}

void __use_true_states()
{
	del_slist(&cur_slist);
	cur_slist = pop_slist(&true_stack);
}

void __use_false_states()
{
	push_slist(&true_stack, clone_slist(cur_slist));
	del_slist(&cur_slist);
	cur_slist = pop_slist(&false_stack);
}

void __pop_false_states()
{
	struct state_list *slist;

	slist = pop_slist(&false_stack);
	del_slist(&slist);
}

void __merge_false_states()
{
	struct state_list *slist;

	slist = pop_slist(&false_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __merge_true_states()
{
	struct state_list *slist;

	slist = pop_slist(&true_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __pop_true_states()
{
	struct state_list *slist;

	slist = pop_slist(&true_stack);
	del_slist(&slist);
}

void __push_continues() 
{ 
	push_slist(&continue_stack, NULL);
}

void __pop_continues() 
{ 
	struct state_list *slist;

	slist = pop_slist(&continue_stack);
	del_slist(&slist);
}

void __process_continues()
{
	struct smatch_state *state;

	FOR_EACH_PTR(cur_slist, state) {
		merge_state_stack(&continue_stack, state->name, state->owner,
				  state->sym, state->state);
	} END_FOR_EACH_PTR(state);
}

void __merge_continues()
{
	struct state_list *slist;

	slist = pop_slist(&continue_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __push_breaks() 
{ 
	push_slist(&break_stack, NULL);
}

void __process_breaks()
{
	struct smatch_state *state;

	FOR_EACH_PTR(cur_slist, state) {
		merge_state_stack(&break_stack, state->name, state->owner, 
				  state->sym, state->state);
	} END_FOR_EACH_PTR(state);
}

void __merge_breaks()
{
	struct state_list *slist;

	slist = pop_slist(&break_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __use_breaks()
{
	del_slist(&cur_slist);
	cur_slist = pop_slist(&break_stack);
}

void __pop_breaks() 
{ 
	struct state_list *slist;

	slist = pop_slist(&break_stack);
	del_slist(&slist);
}

void __save_switch_states() 
{ 
	push_slist(&switch_stack, clone_slist(cur_slist));
}

void __merge_switches()
{
	struct state_list *slist;

	slist = pop_slist(&switch_stack);
	merge_slist(slist);
	push_slist(&switch_stack, slist);
}

void __pop_switches() 
{ 
	struct state_list *slist;

	slist = pop_slist(&switch_stack);
	del_slist(&slist);
}

void __push_default()
{
	push_slist(&default_stack, NULL);
	set_state_stack(&default_stack, "has_default", 0, NULL, 0);
}

void __set_default()
{
	set_state_stack(&default_stack, "has_default", 0, NULL, 1);
}

int __pop_default()
{
	struct state_list *slist;
	struct smatch_state *state;

	int ret = -1;
	slist = pop_slist(&default_stack);
	FOR_EACH_PTR(slist, state) {
		if (!strcmp(state->name, "has_default"))
			ret = state->state;
	} END_FOR_EACH_PTR(state);
	del_slist(&slist);
	return ret;
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
	struct state_list *slist;

	slist = get_slist_from_slist_stack(name);
	if (slist) {
		struct smatch_state *state;
		FOR_EACH_PTR(cur_slist, state) {
			merge_state_slist(&slist, state->name, state->owner,
					  state->sym, state->state);
		} END_FOR_EACH_PTR(state);
		return;
	} else {
		struct state_list *slist;
		struct named_slist *named_slist;
		slist = clone_slist(cur_slist);
		named_slist = alloc_named_slist(name, slist);
		add_ptr_list(&goto_stack, named_slist);
	}
}

void __merge_gotos(const char *name)
{
	struct state_list *slist;
	
	slist = get_slist_from_slist_stack(name);
	merge_slist(slist);
}
