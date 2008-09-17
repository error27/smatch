/*
 * sparse/smatch_slist.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"

ALLOCATOR(sm_state, "smatch state");
ALLOCATOR(named_slist, "named slist");

void add_history(struct sm_state *state)
{
	struct state_history *tmp;

	if (!state)
		return;
	tmp = malloc(sizeof(*tmp));
	tmp->loc = get_lineno();
	add_ptr_list(&state->line_history, tmp);
}

struct sm_state *alloc_state(const char *name, int owner, 
				 struct symbol *sym, int state)
{
	struct sm_state *sm_state = __alloc_sm_state(0);

	sm_state->name = (char *)name;
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->line_history = NULL;
	sm_state->path_history = NULL;
	add_history(sm_state);
	return sm_state;
}

struct sm_state *clone_state(struct sm_state *s)
{
	return alloc_state(s->name, s->owner, s->sym, s->state);
}

struct state_list *clone_slist(struct state_list *from_slist)
{
	struct sm_state *state;
	struct sm_state *tmp;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, state) {
		tmp = clone_state(state);
		add_ptr_list(&to_slist, tmp);
	} END_FOR_EACH_PTR(state);
	return to_slist;
}

int merge_states(const char *name, int owner, struct symbol *sym,
		 int state1, int state2)
{
	int ret;

	if (state1 == state2)
		ret = state1;
	else if (__has_merge_function(owner))
		ret = __client_merge_function(owner, name, sym, 
					      (state1 < state2?state1:state2), 
					      (state1 > state2?state1:state2));
	else 
		ret = UNDEFINED;

	SM_DEBUG("%d merge name='%s' owner=%d: %d + %d => %d\n", 
		 get_lineno(), name, owner, state1, state2, ret);

	return ret;
}

void merge_state_slist(struct state_list **slist, const char *name, int owner,
		       struct symbol *sym, int state)
{
	struct sm_state *tmp;
	int s;

	FOR_EACH_PTR(*slist, tmp) {
		if (tmp->owner == owner && tmp->sym == sym 
		    && !strcmp(tmp->name, name)){
			s = merge_states(name, owner, sym, tmp->state, state);
			if (tmp->state != s) {
				add_history(tmp);
			}
			tmp->state = s;
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	tmp = alloc_state(name, owner, sym, state);
	add_state_slist(slist, tmp);
}

int get_state_slist(struct state_list *slist, const char *name, int owner,
		    struct symbol *sym)
{
	struct sm_state *state;

	if (!name)
		return NOTFOUND;

	FOR_EACH_PTR(slist, state) {
		if (state->owner == owner && state->sym == sym 
		    && !strcmp(state->name, name))
			return state->state;
	} END_FOR_EACH_PTR(state);
	return NOTFOUND;
}

void add_state_slist(struct state_list **slist, struct sm_state *state)
{
	add_ptr_list(slist, state);
}

void set_state_slist(struct state_list **slist, const char *name, int owner,
		     struct symbol *sym, int state)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(*slist, tmp) {
		if (tmp->owner == owner && tmp->sym == sym 
		    && !strcmp(tmp->name, name)){
			tmp->state = state;
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	tmp = alloc_state(name, owner, sym, state);
	add_state_slist(slist, tmp);
}

void delete_state_slist(struct state_list **slist, const char *name, int owner,
			struct symbol *sym)
{
	struct sm_state *state;

	FOR_EACH_PTR(*slist, state) {
		if (state->owner == owner && state->sym == sym 
		    && !strcmp(state->name, name)){
			delete_ptr_list_entry((struct ptr_list **)slist,
					      state, 1);
			__free_sm_state(state);
			return;
		}
	} END_FOR_EACH_PTR(state);
}


void push_slist(struct state_list_stack **list_stack, struct state_list *slist)
{
	add_ptr_list(list_stack, slist);
}

struct state_list *pop_slist(struct state_list_stack **list_stack)
{
	struct state_list *slist;

	slist = last_ptr_list((struct ptr_list *)*list_stack);
	delete_ptr_list_entry((struct ptr_list **)list_stack, slist, 1);
	return slist;
}

void del_slist(struct state_list **slist)
{
	__free_ptr_list((struct ptr_list **)slist);
}

void del_slist_stack(struct state_list_stack **slist_stack)
{
	struct state_list *slist;
	
	FOR_EACH_PTR(*slist_stack, slist) {
		__free_ptr_list((struct ptr_list **)&slist);
	} END_FOR_EACH_PTR(slist);
	__free_ptr_list((struct ptr_list **)slist_stack);	
}

/*
 * set_state_stack() sets the state for the top slist on the stack.
 */
void set_state_stack(struct state_list_stack **stack, const char *name,
		     int owner, struct symbol *sym, int state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	set_state_slist(&slist, name, owner, sym, state);
	push_slist(stack, slist);
}

/*
 * get_state_stack() gets the state for the top slist on the stack.
 */
int get_state_stack(struct state_list_stack *stack, const char *name,
		    int owner, struct symbol *sym)
{
	struct state_list *slist;
	int ret;

	slist = pop_slist(&stack);
	ret = get_state_slist(slist, name, owner, sym);
	push_slist(&stack, slist);
	return ret;
}

void merge_state_stack(struct state_list_stack **stack, const char *name,
		       int owner, struct symbol *sym, int state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	merge_state_slist(&slist, name, owner, sym, state);
	push_slist(stack, slist);
}

void merge_slist(struct state_list *slist)
{
	struct sm_state *state;

	if (!slist) {
		return;
	}

	FOR_EACH_PTR(slist, state) {
		merge_state_slist(&cur_slist, state->name, state->owner,
				  state->sym, state->state);
	} END_FOR_EACH_PTR(state);

	FOR_EACH_PTR(cur_slist, state) {
		if (get_state_slist(slist, state->name, state->owner,
				    state->sym) == NOTFOUND) {
			merge_state_slist(&cur_slist, state->name, state->owner,
					  state->sym, NOTFOUND);
		}
	} END_FOR_EACH_PTR(state);
}

/*
 * and_slist_stack() ands an slist with the top slist in an slist stack.
 */

void and_slist_stack(struct state_list_stack **slist_stack,
		     struct state_list *tmp_slist)
{
	struct sm_state *tmp;
	int tmp_state;

	FOR_EACH_PTR(tmp_slist, tmp) {
		tmp_state = get_state_stack(*slist_stack, tmp->name,
					    tmp->owner, tmp->sym);
		if (tmp_state >= 0 && tmp_state != tmp->state) {
			smatch_msg("wierdness merging 'and' conditions states '%s': %d & %d.\n",
				   tmp->name, tmp_state, tmp->state);
			tmp->state = merge_states(tmp->name, tmp->owner, 
						  tmp->sym, tmp->state,
						  tmp_state);
		}
			
		set_state_stack(slist_stack, tmp->name, tmp->owner, tmp->sym,
				tmp->state);

	} END_FOR_EACH_PTR(tmp);
//	del_slist(&tmp_slist);
}

void or_slist_stack(struct state_list_stack **slist_stack)
{
	struct state_list *one;
	struct state_list *two;
	struct state_list *res = NULL;
 	struct sm_state *tmp;
	int s;

	one = pop_slist(slist_stack);
	two = pop_slist(slist_stack);

	FOR_EACH_PTR(one, tmp) {
		s = get_state_slist(two, tmp->name, tmp->owner, tmp->sym);
		s = merge_states(tmp->name, tmp->owner, tmp->sym,
				 tmp->state, s);
		set_state_slist(&res, tmp->name, tmp->owner, tmp->sym, s);
	} END_FOR_EACH_PTR(tmp);

	push_slist(slist_stack, res);

	del_slist(&one);
	del_slist(&two);
}

struct state_list *get_slist_from_slist_stack(struct slist_stack *stack,
					      const char *name)
{
	struct named_slist *tmp;

	FOR_EACH_PTR(stack, tmp) {
		if (!strcmp(tmp->name, name))
			return tmp->slist;
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

void overwrite_slist(struct state_list *from, struct state_list **to)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(from, tmp) {
		set_state_slist(to, tmp->name, tmp->owner, tmp->sym, tmp->state);
	} END_FOR_EACH_PTR(tmp);
}

