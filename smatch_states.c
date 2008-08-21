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

ALLOCATOR(smatch_state, "smatch state");
DECLARE_PTR_LIST(state_list, struct smatch_state);
DECLARE_PTR_LIST(state_list_stack, struct state_list);

static struct state_list *cur_slist;
static struct state_list_stack *false_stack;
static struct state_list_stack *true_stack;
static struct state_list_stack *break_stack;
static struct state_list_stack *switch_stack;
static struct state_list_stack *default_stack;
static struct state_list_stack *continue_stack;
static struct state_list_stack *mini_false_stack;
static struct state_list_stack *false_only_stack;
static int false_only_prepped = 0;
static struct state_list *and_clumps[2];

struct slist_head {
	char *name;
	struct state_list *slist;
};
ALLOCATOR(slist_head, "goto stack");
DECLARE_PTR_LIST(slist_stack, struct slist_head);
static struct slist_stack *goto_stack;

int debug_states;

void __print_cur_slist()
{
	struct smatch_state *state;

	printf("dumping slist at %d\n", get_lineno());
	FOR_EACH_PTR(cur_slist, state) {
		printf("%s=%d\n", state->name, state->state);
	} END_FOR_EACH_PTR(state);
	printf("---\n");
}

static void add_history(struct smatch_state *state)
{
	struct state_history *tmp;

	if (!state)
		return;
	tmp = malloc(sizeof(*tmp));
	tmp->loc = get_lineno();
	add_ptr_list(&state->line_history, tmp);
}

struct smatch_state *alloc_state(const char *name, int owner, 
					struct symbol *sym, int state)
{
	struct smatch_state *sm_state = __alloc_smatch_state(0);

	sm_state->name = (char *)name;
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->line_history = NULL;
	sm_state->path_history = NULL;
	add_history(sm_state);
	return sm_state;
}

static struct smatch_state *clone_state(struct smatch_state *s)
{
	return alloc_state(s->name, s->owner, s->sym, s->state);
}

static void push_slist(struct state_list_stack **list_stack, 
				   struct state_list *slist)
{
	add_ptr_list(list_stack, slist);
}

static struct state_list *pop_slist(struct state_list_stack **list_stack)
{
	struct state_list *slist;

	slist = last_ptr_list((struct ptr_list *)*list_stack);
	delete_ptr_list_entry((struct ptr_list **)list_stack, slist, 1);
	return slist;
}

static struct state_list *clone_slist(struct state_list *from_slist)
{
	struct smatch_state *state;
	struct smatch_state *tmp;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, state) {
		tmp = clone_state(state);
		add_ptr_list(&to_slist, tmp);
	} END_FOR_EACH_PTR(state);
	return to_slist;
}

static void del_slist(struct state_list **slist)
{
	__free_ptr_list((struct ptr_list **)slist);
}

static void del_slist_stack(struct state_list_stack **slist_stack)
{
	struct state_list *slist;
	
	FOR_EACH_PTR(*slist_stack, slist) {
		__free_ptr_list((struct ptr_list **)&slist);
	} END_FOR_EACH_PTR(slist);
	__free_ptr_list((struct ptr_list **)slist_stack);	
}

static void add_state_slist(struct state_list **slist, 
			    struct smatch_state *state)
{
	add_ptr_list(slist, state);
}

static void set_state_slist(struct state_list **slist, const char *name,
			    int owner, struct symbol *sym, int state)
{
	struct smatch_state *tmp;

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

/*
 * set_state_stack() sets the state for the top slist on the stack.
 */
static inline void set_state_stack(struct state_list_stack **stack, 
				    const char *name, int owner, 
				   struct symbol *sym, int state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	set_state_slist(&slist, name, owner, sym, state);
	push_slist(stack, slist);
}

static int merge_states(const char *name, int owner, struct symbol *sym,
			int state1, int state2)
{
	int ret;


	if (state1 == state2)
		ret = state1;
	else if (__has_merge_function(owner))
		ret = __client_merge_function(owner, name, sym, (state1 < state2?state1:state2), (state1 > state2?state1:state2));
	else 
		ret = UNDEFINED;

	SM_DEBUG("%d merge name='%s' owner=%d: %d + %d => %d\n", 
		 get_lineno(), name, owner, state1, state2, ret);

	return ret;
}

static void merge_state_slist(struct state_list **slist, const char *name, 
			      int owner, struct symbol *sym, int state)
{
	struct smatch_state *tmp;
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

static void merge_state_stack(struct state_list_stack ** stack, 
			      const char *name, int owner, struct symbol *sym,
			      int state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	merge_state_slist(&slist, name, owner, sym, state);
	push_slist(stack, slist);
}

static void delete_state_slist(struct state_list **slist, const char *name,
			       int owner, struct symbol *sym)
{
	struct smatch_state *state;

	FOR_EACH_PTR(*slist, state) {
		if (state->owner == owner && state->sym == sym 
		    && !strcmp(state->name, name)){
			delete_ptr_list_entry((struct ptr_list **)slist,
					      state, 1);
			__free_smatch_state(state);
			return;
		}
	} END_FOR_EACH_PTR(state);
}

static void merge_slist(struct state_list *slist)
{
	struct smatch_state *state;
	FOR_EACH_PTR(slist, state) {
		merge_state_slist(&cur_slist, state->name, state->owner,
				  state->sym, state->state);
	} END_FOR_EACH_PTR(state);
}

void set_state(const char *name, int owner, struct symbol *sym, int state)
{
	struct smatch_state *tmp;

	if (!name)
		return;

	FOR_EACH_PTR(cur_slist, tmp) {
		if (tmp->owner == owner && tmp->sym == sym 
		    && !strcmp(tmp->name, name)){
			SM_DEBUG("%d state change name='%s' owner=%d: %d => %d\n"
				 , get_lineno(), name, owner, tmp->state, state);
			add_history(tmp);
			tmp->state = state;
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	SM_DEBUG("%d new state. name='%s' owner=%d: %d\n", get_lineno(), name,
		 owner, state);
	tmp = alloc_state(name, owner, sym, state);
	add_state_slist(&cur_slist, tmp);

}

static int get_state_slist(struct state_list *slist, const char *name, 
			   int owner, struct symbol *sym)
{
	struct smatch_state *state;

	if (!name)
		return NOTFOUND;

	FOR_EACH_PTR(cur_slist, state) {
		if (state->owner == owner && state->sym == sym 
		    && !strcmp(state->name, name))
			return state->state;
	} END_FOR_EACH_PTR(state);
	return NOTFOUND;
}

int get_state(const char *name, int owner, struct symbol *sym)
{
	return get_state_slist(cur_slist, name, owner, sym);
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
	int merged = merge_states(name, owner, sym, true_state, false_state);

	/* fixme.  history plus don't call get_state() when not needed*/
  	//SM_DEBUG("%d set_true_false %s.  Was %d.  Now T:%d F:%d\n",
	//	 get_lineno(), name, get_state(name, owner, sym), true_state, 
	//	 false_state);

	if (!false_stack || !true_stack || !mini_false_stack) {
		printf("Error:  missing true/false stacks\n");
		return;
	}
	if (__negate()) {
		int tmp = true_state;
		
		true_state = false_state;
		false_state = tmp;
	}
	set_state(name, owner, sym, true_state);
	set_state_stack(&mini_false_stack, name, owner, sym, false_state);
	set_state_slist(&and_clumps[1], name, owner, sym, true_state);
	set_state_stack(&true_stack, name, owner, sym,
			(__ors?merged:true_state));
	set_state_stack(&false_stack, name, owner, sym,
			(__ands?merged:false_state));
	if (false_only_prepped)
		set_state_stack(&false_only_stack, name, owner, sym, false_state);
}

void delete_state(const char *name, int owner, struct symbol *sym)
{
	delete_state_slist(&cur_slist, name, owner, sym);
}

void nullify_path()
{
	del_slist(&cur_slist);
}

void clear_all_states()
{
	struct slist_head *slist_head;

	nullify_path();
	del_slist_stack(&false_stack);
	del_slist_stack(&true_stack);
	del_slist_stack(&break_stack);
	del_slist_stack(&switch_stack);
	del_slist_stack(&continue_stack);
	del_slist_stack(&mini_false_stack);
	del_slist(&and_clumps[0]);
	del_slist(&and_clumps[1]);

	FOR_EACH_PTR(goto_stack, slist_head) {
		del_slist(&slist_head->slist);
	} END_FOR_EACH_PTR(slist_head);
	__free_ptr_list((struct ptr_list **)&goto_stack);
}

void __first_and_clump()
{
	del_slist(&and_clumps[0]);
	and_clumps[0] = and_clumps[1];
	and_clumps[1] = NULL;
}

void __merge_and_clump()
{
	struct smatch_state *tmp;

	FOR_EACH_PTR(and_clumps[0], tmp) {
		if (tmp->state !=  get_state_slist(and_clumps[1], tmp->name, 
						   tmp->owner, tmp->sym))
			DELETE_CURRENT_PTR(tmp);
	} END_FOR_EACH_PTR(tmp);
	del_slist(&and_clumps[1]);
}

void __use_and_clumps()
{
	struct smatch_state *tmp;

	FOR_EACH_PTR(and_clumps[0], tmp) {
		if (tmp) 
			set_state_stack(&true_stack, tmp->name, tmp->owner,
					tmp->sym, tmp->state);
	} END_FOR_EACH_PTR(tmp);
	del_slist(&and_clumps[0]);
}

void __split_false_states_mini()
{
	push_slist(&mini_false_stack, clone_slist(cur_slist));
}

void __use_false_states_mini()
{
	struct state_list *slist;

	nullify_path();
	slist = pop_slist(&mini_false_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __pop_false_states_mini()
{
	struct state_list *slist;

	slist = pop_slist(&mini_false_stack);
	del_slist(&slist);
}

void __prep_false_only_stack()
{
	push_slist(&false_only_stack, NULL);
	false_only_prepped = 1;
}

void __use_false_only_stack()
{
	struct state_list *slist;
	struct smatch_state *tmp;

	slist = pop_slist(&false_only_stack);
	FOR_EACH_PTR(slist, tmp) {
		set_state(tmp->name, tmp->owner, tmp->sym, tmp->state);
	} END_FOR_EACH_PTR(tmp);
	del_slist(&slist);
	false_only_prepped = 0;
}

void __split_true_false_paths()
{
	push_slist(&false_stack, clone_slist(cur_slist));
	push_slist(&true_stack, clone_slist(cur_slist));
	__split_false_states_mini();
}

void __use_true_states()
{
	struct state_list *slist;

	__pop_false_states_mini();
	nullify_path();
	slist = pop_slist(&true_stack);
	merge_slist(slist);
	del_slist(&slist);
}

void __use_false_states()
{
	struct state_list *slist;
	
	push_slist(&true_stack, clone_slist(cur_slist));
	nullify_path();
	slist = pop_slist(&false_stack);
	merge_slist(slist);
	del_slist(&slist);
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

static struct slist_head *alloc_slist_head(const char *name, 
					 struct state_list *slist)
{
	struct slist_head *slist_head = __alloc_slist_head(0);

	slist_head->name = (char *)name;
	slist_head->slist = slist;
	return slist_head;
}

static struct state_list *get_slist_from_slist_stack(const char *name)
{
	struct slist_head *tmp;

	FOR_EACH_PTR(goto_stack, tmp) {
		if (!strcmp(tmp->name, name))
			return tmp->slist;
	} END_FOR_EACH_PTR(tmp);
	return NULL;
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
		struct slist_head *slist_head;
		slist = clone_slist(cur_slist);
		slist_head = alloc_slist_head(name, slist);
		add_ptr_list(&goto_stack, slist_head);
	}
}

void __merge_gotos(const char *name)
{
	struct state_list *slist;
	
	slist = get_slist_from_slist_stack(name);
	merge_slist(slist);
}
