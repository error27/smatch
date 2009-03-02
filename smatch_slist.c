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
#include "smatch_slist.h"

#undef CHECKORDER

ALLOCATOR(sm_state, "smatch state");
ALLOCATOR(named_slist, "named slist");

void __print_slist(struct state_list *slist)
{
	struct sm_state *state;
	struct sm_state *poss;
	int i;

	printf("dumping slist at %d\n", get_lineno());
	FOR_EACH_PTR(slist, state) {
		printf("%d '%s'=%s (", state->owner, state->name,
			show_state(state->state));
		i = 0;
		FOR_EACH_PTR(state->possible, poss) {
			if (i++)
				printf(", ");
			printf("%s", show_state(poss->state));
		} END_FOR_EACH_PTR(poss);
		printf(")\n");
	} END_FOR_EACH_PTR(state);
	printf("---\n");
}


/* NULL states go at the end to simplify merge_slist */
int cmp_tracker(const struct sm_state *a, const struct sm_state *b)
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

static int cmp_sm_states(const struct sm_state *a, const struct sm_state *b)
{
	int ret;

	ret = cmp_tracker(a, b);
	if (ret)
		return ret;

	/* todo:  add hook for smatch_extra.c */
	if (a->state > b->state)
		return -1;
	if (a->state < b->state)
		return 1;
	return 0;
}

void add_sm_state_slist(struct state_list **slist, struct sm_state *new)
{
 	struct sm_state *tmp;

 	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_sm_states(tmp, new) < 0)
			continue;
		else if (cmp_sm_states(tmp, new) == 0) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(slist, new);
}

static void add_possible(struct sm_state *sm, struct sm_state *new)
{
	struct sm_state *tmp;
	struct sm_state *tmp2;

	if (!new) {
		struct smatch_state *s;

		s = merge_states(sm->name, sm->owner, sm->sym, sm->state, NULL);
		tmp = alloc_state(sm->name, sm->owner, sm->sym, s);
		add_sm_state_slist(&sm->possible, tmp);
		return;
	}

	FOR_EACH_PTR(new->possible, tmp) {
		tmp2 = alloc_state(tmp->name, tmp->owner, tmp->sym, tmp->state);
		add_sm_state_slist(&sm->possible, tmp2);
	} END_FOR_EACH_PTR(tmp);
}

struct sm_state *alloc_state(const char *name, int owner, 
			     struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *sm_state = __alloc_sm_state(0);

	sm_state->name = (char *)name;
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->my_pools = NULL;
	sm_state->all_pools = NULL;
	sm_state->possible = NULL;
	add_ptr_list(&sm_state->possible, sm_state);
	return sm_state;
}

/* At the end of every function we free all the sm_states */
void free_every_single_sm_state()
{
	struct allocator_struct *desc = &sm_state_allocator;
	struct allocation_blob *blob = desc->blobs;

	desc->blobs = NULL;
	desc->allocations = 0;
	desc->total_bytes = 0;
	desc->useful_bytes = 0;
	desc->freelist = NULL;
	while (blob) {
		struct allocation_blob *next = blob->next;
		struct sm_state *sm = (struct sm_state *)blob->data;

		free_slist(&sm->possible);
		free_stack(&sm->my_pools);
		free_stack(&sm->all_pools);
		blob_free(blob, desc->chunking);
		blob = next;
	}
}

struct sm_state *clone_state(struct sm_state *s)
{
	struct sm_state *ret;
	struct sm_state *tmp;
	struct sm_state *poss;

	ret = alloc_state(s->name, s->owner, s->sym, s->state);
	ret->my_pools = clone_stack(s->my_pools);
	ret->all_pools = clone_stack(s->all_pools);
	FOR_EACH_PTR(s->possible, poss) {
		tmp = alloc_state(s->name, s->owner, s->sym, poss->state);
		add_sm_state_slist(&ret->possible, tmp);	
	} END_FOR_EACH_PTR(poss);
	return ret;
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
		if (last && cmp_tracker(state, last) <= 0) {
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
	else if (!state1 || !state2)
		ret = &undefined;
	else 
		ret = &merged;
	return ret;
}

/*
 * add_pool() adds a slist to ->pools. If the slist has already been
 * added earlier then it doesn't get added a second time.
 */
static void add_pool(struct state_list_stack **pools, struct state_list *new)
{
	struct state_list *tmp;

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

static void copy_pools(struct sm_state *to, struct sm_state *sm)
{
	struct state_list *tmp;

	if (!sm)
		return;

 	FOR_EACH_PTR(sm->my_pools, tmp) {
		add_pool(&to->my_pools, tmp);
	} END_FOR_EACH_PTR(tmp);

 	FOR_EACH_PTR(sm->all_pools, tmp) {
		add_pool(&to->all_pools, tmp);
	} END_FOR_EACH_PTR(tmp);
}

struct sm_state *merge_sm_states(struct sm_state *one, struct sm_state *two)
{
	struct smatch_state *s;
	struct sm_state *result;

	s = merge_states(one->name, one->owner, one->sym, one->state,
			(two?two->state:NULL));
	result = alloc_state(one->name, one->owner, one->sym, s);
	add_possible(result, one);
	add_possible(result, two);
	copy_pools(result, one);
	copy_pools(result, two);

	if (debug_states) {
		struct sm_state *tmp;
		int i = 0;

		printf("%d merge name='%s' owner=%d: %s + %s => %s (", 
			get_lineno(), one->name, one->owner,
			show_state(one->state), show_state(two?two->state:NULL),
			show_state(s));

		FOR_EACH_PTR(result->possible, tmp) {
			if (i++) {
				printf(", ");
			}
			printf("%s", show_state(tmp->state));
		} END_FOR_EACH_PTR(tmp);
		printf(")\n");
	}

	return result;
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

void overwrite_sm_state(struct state_list **slist, struct sm_state *state)
{
 	struct sm_state *tmp;
	struct sm_state *new = clone_state(state); //fixme. why?
 
 	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_tracker(tmp, new) < 0)
			continue;
		else if (cmp_tracker(tmp, new) == 0) {
			REPLACE_CURRENT_PTR(tmp, new);
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(slist, new);
}

void overwrite_sm_state_stack(struct state_list_stack **stack,
			struct sm_state *state)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	overwrite_sm_state(&slist, state);
	push_slist(stack, slist);
}

void set_state_slist(struct state_list **slist, const char *name, int owner,
 		     struct symbol *sym, struct smatch_state *state)
{
 	struct sm_state *tmp;
	struct sm_state *new = alloc_state(name, owner, sym, state);

 	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_tracker(tmp, new) < 0)
			continue;
		else if (cmp_tracker(tmp, new) == 0) {
			tmp->state = state;
			tmp->my_pools = NULL;
			tmp->all_pools = NULL;
			tmp->possible = NULL;
			add_ptr_list(&tmp->possible, tmp);
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

void free_slist(struct state_list **slist)
{
	__free_ptr_list((struct ptr_list **)slist);
}

void free_stack(struct state_list_stack **stack)
{
	__free_ptr_list((struct ptr_list **)stack);
}

void free_stack_and_slists(struct state_list_stack **slist_stack)
{
	struct state_list *slist;
	
	FOR_EACH_PTR(*slist_stack, slist) {
		free_slist(&slist);
	} END_FOR_EACH_PTR(slist);
	free_stack(slist_stack);	
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

/*
 * merge_slist() is called whenever paths merge, such as after
 * an if statement.  It takes the two slists and creates one.
 */
void merge_slist(struct state_list **to, struct state_list *slist)
{
	struct sm_state *to_state, *state, *tmp;
	struct state_list *results = NULL;
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

	implied_to = clone_slist(*to);
	implied_from = clone_slist(slist);

	PREPARE_PTR_LIST(*to, to_state);
	PREPARE_PTR_LIST(slist, state);
	for (;;) {
		if (!to_state && !state)
			break;
		if (cmp_tracker(to_state, state) < 0) {
			tmp = merge_sm_states(to_state, NULL);
			add_pool(&tmp->my_pools, implied_to);
			add_pool(&tmp->all_pools, implied_to);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(to_state);
		} else if (cmp_tracker(to_state, state) == 0) {
			tmp = merge_sm_states(to_state, state);
			if (!is_really_same(to_state, state)) {
				add_pool(&tmp->my_pools, implied_to);
				add_pool(&tmp->my_pools, implied_from);
			}
			add_pool(&tmp->all_pools, implied_to);
			add_pool(&tmp->all_pools, implied_from);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(to_state);
			NEXT_PTR_LIST(state);
		} else {
			tmp = merge_sm_states(state, NULL);
			add_pool(&tmp->my_pools, implied_from);
			add_pool(&tmp->all_pools, implied_from);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(state);
		}
	}
	FINISH_PTR_LIST(state);
	FINISH_PTR_LIST(to_state);

	free_slist(to);
	*to = results;

	push_slist(&implied_pools, implied_from);
	push_slist(&implied_pools, implied_to);
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
struct sm_state *merge_implied(struct sm_state *one, struct sm_state *two,
				struct state_list *pool,
				struct state_list *cur_slist)
{
	struct sm_state *cur_state;

	// fixme:  do we not need to check this?
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
void filter(struct state_list **slist, struct state_list *filter,
	struct state_list *cur_slist)
{
	struct sm_state *s_one, *s_two;
	struct state_list *results = NULL;
	struct sm_state *tmp;

#ifdef CHECKORDER
	check_order(*slist);
	check_order(filter);
#endif

	PREPARE_PTR_LIST(*slist, s_one);
	PREPARE_PTR_LIST(filter, s_two);
	for (;;) {
		if (!s_one || !s_two)
			break;
		if (cmp_tracker(s_one, s_two) < 0) {
			NEXT_PTR_LIST(s_one);
		} else if (cmp_tracker(s_one, s_two) == 0) {
			tmp = merge_implied(s_one, s_two, filter, cur_slist);
			if (tmp)
				add_ptr_list(&results, tmp);
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

/*
 * and_slist_stack() is basically the same as popping the top two slists,
 * overwriting the one with the other and pushing it back on the stack.
 * The difference is that it checks to see that a mutually exclusive
 * state isn't included in both stacks.  If smatch sees something like
 * "if (a && !a)" it prints a warning.
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
				"'%s': %s + %s",
				tmp->name, show_state(tmp_state),
				show_state(tmp->state));
		}
		set_state_stack(slist_stack, tmp->name, tmp->owner, tmp->sym,
				tmp->state);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&tmp_slist);
}

/* 
 * or_slist_stack() is for if we have:  if (foo || bar) { foo->baz;
 * It pops the two slists from the top of the stack and merges them
 * together in a way that preserves the things they have in common
 * but creates a merged state for most of the rest.
 * You could have code that had:  if (foo || foo) { foo->baz;
 * It's this function which ensures smatch does the right thing.
 */
void or_slist_stack(struct state_list_stack **slist_stack)
{
	struct state_list *one;
	struct state_list *two;
	struct state_list *res = NULL;
 	struct sm_state *tmp;
 	struct sm_state *sm;
 	struct sm_state *new_sm;

	one = pop_slist(slist_stack);
	two = pop_slist(slist_stack);

	FOR_EACH_PTR(one, tmp) {
		sm = get_sm_state_slist(two, tmp->name, tmp->owner, tmp->sym);
		new_sm = merge_sm_states(tmp, sm);
		add_ptr_list(&res, new_sm);
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(two, tmp) {
		sm = get_sm_state_slist(one, tmp->name, tmp->owner, tmp->sym);
		new_sm = merge_sm_states(tmp, sm);
		add_ptr_list(&res, new_sm);
	} END_FOR_EACH_PTR(tmp);

	push_slist(slist_stack, res);

	free_slist(&one);
	free_slist(&two);
}

/*
 * get_slist_from_named_stack() is only used for gotos.
 */
struct state_list **get_slist_from_named_stack(struct named_stack *stack,
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
