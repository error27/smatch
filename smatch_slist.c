/*
 * sparse/smatch_slist.c
 *
 * Copyright (C) 2008,2009 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch.h"
#include "smatch_slist.h"

ALLOCATOR(sm_state, "smatch state");
ALLOCATOR(named_slist, "named slist");

#undef CHECKORDER

void __print_slist(struct state_list *slist)
{
	struct sm_state *state;

	printf("dumping slist at %d\n", get_lineno());
	FOR_EACH_PTR(slist, state) {
		printf("%d '%s'=%s\n", state->owner, state->name,
			show_state(state->state));
	} END_FOR_EACH_PTR(state);
	printf("---\n");
}

void add_history(struct sm_state *state)
{
	struct state_history *tmp;

	if (!state)
		return;
	tmp = malloc(sizeof(*tmp));
	tmp->loc = get_lineno();
	add_ptr_list(&state->line_history, tmp);
}

static void add_possible(struct sm_state *sm, struct sm_state *new)
{
 	struct sm_state *tmp;

 	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state < new->state) {
			continue;
		} else if (tmp->state == new->state) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(&sm->possible, new);
}

struct sm_state *alloc_state(const char *name, int owner, 
			     struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *sm_state = __alloc_sm_state(0);

	sm_state->name = (char *)name;
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->line_history = NULL;
	add_history(sm_state);
	sm_state->pools = NULL;
	sm_state->possible = NULL;
	add_possible(sm_state, sm_state);
	return sm_state;
}

struct sm_state *clone_state(struct sm_state *s)
{
	struct sm_state *tmp;

	tmp = alloc_state(s->name, s->owner, s->sym, s->state);
	tmp->pools = clone_stack(s->pools);
	return tmp;
}

/* NULL states go at the end to simplify merge_slist */
static int cmp_sm_states(const struct sm_state *a, const struct sm_state *b)
{
	int ret;

	if (!a && !b)
		return 0;
	if (!b)
		return -1;
	if (!a)
		return 1;

	if (a->owner > b->owner)
		return -1;
	if (a->owner < b->owner)
		return 1;

	ret = strcmp(a->name, b->name);
	if (ret)
		return ret;

	if (!b->sym && a->sym)
		return -1;
	if (!a->sym && b->sym)
		return 1;
	if (a->sym > b->sym)
		return -1;
	if (a->sym < b->sym)
		return 1;

	return 0;
}

int slist_has_state(struct state_list *slist, struct smatch_state *state)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == state)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

#ifdef CHECKORDER
static void check_order(struct state_list *slist)
{
	struct sm_state *state;
	struct sm_state *last = NULL;
	int printed = 0;

	FOR_EACH_PTR(slist, state) {
		if (last && cmp_sm_states(state, last) <= 0) {
			printf("Error.  Unsorted slist %d vs %d, %p vs %p, "
			       "%s vs %s\n", last->owner, state->owner, 
			       last->sym, state->sym, last->name, state->name);
			printed = 1;
		}
		last = state;
	} END_FOR_EACH_PTR(state);

	if (printed)
		printf("======\n");
}
#endif

struct state_list *clone_slist(struct state_list *from_slist)
{
	struct sm_state *state;
	struct sm_state *tmp;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, state) {
		tmp = clone_state(state);
		add_ptr_list(&to_slist, tmp);
	} END_FOR_EACH_PTR(state);
#ifdef CHECKORDER
	check_order(to_slist);
#endif
	return to_slist;
}

struct state_list_stack *clone_stack(struct state_list_stack *from_stack)
{
	struct state_list *slist;
	struct state_list_stack *to_stack = NULL;

	FOR_EACH_PTR(from_stack, slist) {
		push_slist(&to_stack, slist);
	} END_FOR_EACH_PTR(slist);
	return to_stack;
}

// FIXME...  shouldn't we free some of these state pointers?
struct smatch_state *merge_states(const char *name, int owner,
				  struct symbol *sym,
				  struct smatch_state *state1,
				  struct smatch_state *state2)
{
	struct smatch_state *ret;

	if (state1 == state2)
		ret = state1;
	else if (__has_merge_function(owner))
		ret = __client_merge_function(owner, name, sym, state1, state2); 
	else 
		ret = &merged;

	SM_DEBUG("%d merge name='%s' owner=%d: %s + %s => %s\n", 
		 get_lineno(), name, owner, show_state(state1), 
		 show_state(state2), show_state(ret));

	return ret;
}

struct sm_state *get_sm_state_slist(struct state_list *slist, const char *name,
				int owner, struct symbol *sym)
{
	struct sm_state *state;

	if (!name)
		return NULL;

	FOR_EACH_PTR(slist, state) {
		if (state->owner == owner && state->sym == sym 
		    && !strcmp(state->name, name))
			return state;
	} END_FOR_EACH_PTR(state);
	return NULL;
}

struct smatch_state *get_state_slist(struct state_list *slist, 
				const char *name, int owner,
				struct symbol *sym)
{
	struct sm_state *state;

	state = get_sm_state_slist(slist, name, owner, sym);
	if (state)
		return state->state;
	return NULL;
}

static void overwrite_sm_state(struct state_list **slist,
			       struct sm_state *state)
{
 	struct sm_state *tmp;
	struct sm_state *new = clone_state(state);
 
 	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_sm_states(tmp, new) < 0)
			continue;
		else if (cmp_sm_states(tmp, new) == 0) {
			tmp->state = new->state;
			tmp->pools = new->pools;
			tmp->possible = new->possible;
			__free_sm_state(new);
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(slist, new);
}

void set_state_slist(struct state_list **slist, const char *name, int owner,
 		     struct symbol *sym, struct smatch_state *state)
{
 	struct sm_state *tmp;
	struct sm_state *new = alloc_state(name, owner, sym, state);

 	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_sm_states(tmp, new) < 0)
			continue;
		else if (cmp_sm_states(tmp, new) == 0) {
			tmp->state = state;
			tmp->pools = NULL;
			tmp->possible = new->possible;
			__free_sm_state(new);
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(slist, new);
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
	delete_ptr_list_last((struct ptr_list **)list_stack);
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
		     int owner, struct symbol *sym, struct smatch_state *state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	set_state_slist(&slist, name, owner, sym, state);
	push_slist(stack, slist);
}

/*
 * get_state_stack() gets the state for the top slist on the stack.
 */
struct smatch_state *get_state_stack(struct state_list_stack *stack,
				     const char *name, int owner,
				     struct symbol *sym)
{
	struct state_list *slist;
	struct smatch_state *ret;

	slist = pop_slist(&stack);
	ret = get_state_slist(slist, name, owner, sym);
	push_slist(&stack, slist);
	return ret;
}

static void add_pool(struct sm_state *state, struct state_list **pool)
{
	add_ptr_list(pool, state);
	push_slist(&state->pools, *pool);
}

/* fixme.  rename.  heck cleanup this whole file */
static void add_single_pool(struct sm_state *to, struct state_list *new)
{
	struct state_list *tmp;

	FOR_EACH_PTR(to->pools, tmp) {
		if (tmp < new)
			continue;
		else if (tmp == new) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(&to->pools, new);
}

static void copy_pools(struct sm_state *to, struct sm_state *sm)
{
	struct state_list *tmp;

 	FOR_EACH_PTR(sm->pools, tmp) {
		add_single_pool(to, tmp);
	} END_FOR_EACH_PTR(tmp);
}

void merge_slist(struct state_list **to, struct state_list *slist)
{
	struct sm_state *to_state, *state, *tmp;
	struct state_list *results = NULL;
	struct smatch_state *s;
	struct state_list *implied_to = NULL;
	struct state_list *implied_from = NULL;

#ifdef CHECKORDER
	check_order(*to);
	check_order(slist);
#endif

	/* merging a null and nonnull path gives you only the nonnull path */
	if (!slist) {
		return;
	}
	if (!*to) {
		*to = clone_slist(slist);
		return;
	}

	PREPARE_PTR_LIST(*to, to_state);
	PREPARE_PTR_LIST(slist, state);
	for (;;) {
		if (!to_state && !state)
			break;
		if (cmp_sm_states(to_state, state) < 0) {
			s = merge_states(to_state->name, to_state->owner,
					 to_state->sym, to_state->state, NULL);
			tmp = alloc_state(to_state->name, to_state->owner,
					  to_state->sym, s);
			//add_possible(to_state, NULL);
			add_pool(to_state, &implied_to);
			copy_pools(tmp, to_state);
			add_single_pool(tmp, implied_to);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(to_state);
		} else if (cmp_sm_states(to_state, state) == 0) {
			if (to_state->state == state->state) {
				s = to_state->state;
				tmp = alloc_state(to_state->name, 
						to_state->owner,
						to_state->sym, s);

			} else {
				s = merge_states(to_state->name,
						 to_state->owner,
						 to_state->sym, to_state->state,
						 state->state);

				tmp = alloc_state(to_state->name,
						to_state->owner,
						to_state->sym, s);


				add_ptr_list(&implied_to, to_state);
				add_ptr_list(&implied_from, state);
				copy_pools(tmp, to_state);
				copy_pools(tmp, state);
				add_single_pool(tmp, implied_to);
				add_single_pool(tmp, implied_from);
			}
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(to_state);
			NEXT_PTR_LIST(state);
		} else {
			s = merge_states(state->name, state->owner,
					 state->sym, state->state, NULL);
			tmp = alloc_state(state->name, state->owner,
					  state->sym, s);
			//add_possible(state, NULL);
			add_pool(state, &implied_from);
			copy_pools(tmp, state);
			add_single_pool(tmp, implied_from);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(state);
		}
	}
	FINISH_PTR_LIST(state);
	FINISH_PTR_LIST(to_state);

	del_slist(to);
	*to = results;

	if (implied_from)
		push_slist(&implied_pools, implied_from);
	if (implied_to)
		push_slist(&implied_pools, implied_to);
}

/*
 * If you call set_state() then you need to remove
 * the state from any pools it's in.  Just setting ->pools
 * to NULL isn't enough.
 */
void remove_from_pools(struct sm_state *old)
{
	struct state_list *slist;

	if (!old)
		return;

	FOR_EACH_PTR(old->pools, slist) {
		delete_state_slist(&slist, old->name, old->owner, old->sym);
	} END_FOR_EACH_PTR(slist);
}


/*
 * filter() is used to find what states are the same across
 * a series of slists.
 * It takes a **slist and a *filter.  
 * It removes everything from **slist that isn't in *filter.
 * The reason you would want to do this is if you want to 
 * know what other states are true if one state is true.  (smatch_implied).
 */

void filter(struct state_list **slist, struct state_list *filter)
{
	struct sm_state *s_one, *s_two;
	struct state_list **results;

#ifdef CHECKORDER
	check_order(*slist);
	check_order(filter);
#endif

	results = malloc(sizeof(*results));
	*results = NULL;

	PREPARE_PTR_LIST(*slist, s_one);
	PREPARE_PTR_LIST(filter, s_two);
	for (;;) {
		if (!s_one || !s_two)
			break;
		if (cmp_sm_states(s_one, s_two) < 0) {
			SM_DEBUG("different missing in new %s\n", s_one->name);
			NEXT_PTR_LIST(s_one);
		} else if (cmp_sm_states(s_one, s_two) == 0) {
			if (s_one->state == s_two->state) {
				add_ptr_list(results, s_one);
				SM_DEBUG("same %s\n", s_one->name);
			} else
				SM_DEBUG("different %s %s vs %s\n", s_one->name,
				       show_state(s_one->state), 
				       show_state(s_two->state));
			NEXT_PTR_LIST(s_one);
			NEXT_PTR_LIST(s_two);
		} else {
			SM_DEBUG("different missing in old%s\n", s_two->name);
			NEXT_PTR_LIST(s_two);
		}
	}
	FINISH_PTR_LIST(s_two);
	FINISH_PTR_LIST(s_one);

	del_slist(slist);
	*slist = *results;
}

/*
 * This function is basically the same as popping the top two slists,
 * overwriting the one with the other and pushing it back on the stack.
 * The difference is that it checks to see that a mutually exclusive
 * state isn't included in both stacks.
 */

void and_slist_stack(struct state_list_stack **slist_stack)
{
     	struct sm_state *tmp;
	struct smatch_state *tmp_state;
	struct state_list *tmp_slist = pop_slist(slist_stack);

	FOR_EACH_PTR(tmp_slist, tmp) {
		tmp_state = get_state_stack(*slist_stack, tmp->name,
					    tmp->owner, tmp->sym);
		if (tmp_state && tmp_state != tmp->state) {
			smatch_msg("mutually exclusive 'and' conditions states "
				   "'%s': %s & %s.\n",
				   tmp->name, show_state(tmp_state),
				   show_state(tmp->state));
			tmp->state = merge_states(tmp->name, tmp->owner, 
						  tmp->sym, tmp->state,
						  tmp_state);
		}
		set_state_stack(slist_stack, tmp->name, tmp->owner, tmp->sym,
				tmp->state);

	} END_FOR_EACH_PTR(tmp);
	del_slist(&tmp_slist);
}

void or_slist_stack(struct state_list_stack **slist_stack)
{
	struct state_list *one;
	struct state_list *two;
	struct state_list *res = NULL;
 	struct sm_state *tmp;
	struct smatch_state *s;

	one = pop_slist(slist_stack);
	two = pop_slist(slist_stack);

	FOR_EACH_PTR(one, tmp) {
		s = get_state_slist(two, tmp->name, tmp->owner, tmp->sym);
		s = merge_states(tmp->name, tmp->owner, tmp->sym,
				 tmp->state, s);
		set_state_slist(&res, tmp->name, tmp->owner, tmp->sym, s);
	} END_FOR_EACH_PTR(tmp);


	FOR_EACH_PTR(two, tmp) {
		s = get_state_slist(one, tmp->name, tmp->owner, tmp->sym);
		s = merge_states(tmp->name, tmp->owner, tmp->sym,
				 tmp->state, s);
		set_state_slist(&res, tmp->name, tmp->owner, tmp->sym, s);
	} END_FOR_EACH_PTR(tmp);

	push_slist(slist_stack, res);

	del_slist(&one);
	del_slist(&two);
}

struct state_list **get_slist_from_slist_stack(struct slist_stack *stack,
					      const char *name)
{
	struct named_slist *tmp;

	FOR_EACH_PTR(stack, tmp) {
		if (!strcmp(tmp->name, name))
			return &tmp->slist;
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

void overwrite_slist(struct state_list *from, struct state_list **to)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(from, tmp) {
		overwrite_sm_state(to, tmp);
	} END_FOR_EACH_PTR(tmp);
}

