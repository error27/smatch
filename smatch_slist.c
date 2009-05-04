/*
 * sparse/smatch_slist.c
 *
 * Copyright (C) 2008,2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

#undef CHECKORDER

ALLOCATOR(smatch_state, "smatch state");
ALLOCATOR(sm_state, "sm state");
ALLOCATOR(named_slist, "named slist");
__DO_ALLOCATOR(char, 0, 1, "state names", sname);

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

	if (a == b)
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

static struct sm_state *alloc_state_no_name(const char *name, int owner, 
				     struct symbol *sym,
				     struct smatch_state *state)
{
	struct sm_state *tmp;

	tmp = alloc_state(NULL, owner, sym, state);
	tmp->name = name;
	return tmp;
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
		tmp = alloc_state_no_name(sm->name, sm->owner, sm->sym, s);
		add_sm_state_slist(&sm->possible, tmp);
		return;
	}

	FOR_EACH_PTR(new->possible, tmp) {
		tmp2 = alloc_state_no_name(tmp->name, tmp->owner, tmp->sym,
					   tmp->state);
		add_sm_state_slist(&sm->possible, tmp2);
	} END_FOR_EACH_PTR(tmp);
}

char *alloc_sname(const char *str)
{
	char *tmp;

	if (!str)
		return NULL;
	tmp = __alloc_sname(strlen(str) + 1);
	strcpy(tmp, str);
	return tmp;
}

struct sm_state *alloc_state(const char *name, int owner, 
			     struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *sm_state = __alloc_sm_state(0);

	sm_state->name = alloc_sname(name);
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->line = get_lineno();
	sm_state->merged = 0;
	sm_state->my_pools = NULL;
	sm_state->pre_merge = NULL;
	sm_state->possible = NULL;
	add_ptr_list(&sm_state->possible, sm_state);
	return sm_state;
}

static void free_sm_state(struct sm_state *sm)
{
	free_slist(&sm->possible);
	free_slist(&sm->pre_merge);
	free_stack(&sm->my_pools);
	/* 
	 * fixme.  Free the actual state.
	 * Right now we leave it until the end of the function
	 * because we don't want to double free it.
	 * Use the freelist to not double free things 
	 */
}

static void free_all_sm_states(struct allocation_blob *blob)
{
	unsigned int size = sizeof(struct sm_state);
	unsigned int offset = 0;

	while (offset < blob->offset) {
		free_sm_state((struct sm_state *)(blob->data + offset));
		offset += size;
	}
}

/* At the end of every function we free all the sm_states */
void free_every_single_sm_state(void)
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
		free_all_sm_states(blob);
		blob_free(blob, desc->chunking);
		blob = next;
	}
	clear_sname_alloc();
}

struct sm_state *clone_state(struct sm_state *s)
{
	struct sm_state *ret;

	ret = alloc_state_no_name(s->name, s->owner, s->sym, s->state);
	ret->line = s->line;
	ret->merged = s->merged;
	ret->my_pools = clone_stack(s->my_pools);
	ret->possible = clone_slist(s->possible);
	ret->pre_merge = clone_slist(s->pre_merge);
	return ret;
}

int is_merged(struct sm_state *sm)
{
	return sm->merged;
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

static void check_order(struct state_list *slist)
{
#ifdef CHECKORDER
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
#endif
}

struct state_list *clone_slist(struct state_list *from_slist)
{
	struct sm_state *state;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, state) {
		add_ptr_list(&to_slist, state);
	} END_FOR_EACH_PTR(state);
	check_order(to_slist);
	return to_slist;
}

struct state_list *clone_slist_and_states(struct state_list *from_slist)
{
	struct sm_state *state;
	struct sm_state *tmp;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, state) {
		tmp = clone_state(state);
		add_ptr_list(&to_slist, tmp);
	} END_FOR_EACH_PTR(state);
	check_order(to_slist);
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
void add_pool(struct state_list_stack **pools, struct state_list *new)
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

void merge_pools(struct state_list_stack **to, struct state_list_stack *from)
{
	struct state_list *tmp;

 	FOR_EACH_PTR(from, tmp) {
		add_pool(to, tmp);
	} END_FOR_EACH_PTR(tmp);
}

struct sm_state *merge_sm_states(struct sm_state *one, struct sm_state *two)
{
	struct smatch_state *s;
	struct sm_state *result;

	if (one == two)
		return one;
	s = merge_states(one->name, one->owner, one->sym, one->state,
			(two?two->state:NULL));
	result = alloc_state_no_name(one->name, one->owner, one->sym, s);
	if (two && one->line == two->line)
		result->line = one->line;
	result->merged = 1;
	add_ptr_list(&result->pre_merge, one);
	add_ptr_list(&result->pre_merge, two);
	add_possible(result, one);
	add_possible(result, two);

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

void overwrite_sm_state(struct state_list **slist, struct sm_state *new)
{
 	struct sm_state *tmp;
 
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
			REPLACE_CURRENT_PTR(tmp, new);
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
			DELETE_CURRENT_PTR(state);
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
 * get_sm_state_stack() gets the state for the top slist on the stack.
 */
struct sm_state *get_sm_state_stack(struct state_list_stack *stack,
				    const char *name, int owner,
				    struct symbol *sym)
{
	struct state_list *slist;
	struct sm_state *ret;

	slist = pop_slist(&stack);
	ret = get_sm_state_slist(slist, name, owner, sym);
	push_slist(&stack, slist);
	return ret;
}


struct smatch_state *get_state_stack(struct state_list_stack *stack,
				     const char *name, int owner,
				     struct symbol *sym)
{
	struct sm_state *state;

	state = get_sm_state_stack(stack, name, owner, sym);
	if (state)
		return state->state;
	return NULL;
}

static void match_states(struct state_list **one, struct state_list **two)
{
	struct sm_state *one_state;
	struct sm_state *two_state;
	struct sm_state *tmp;
	struct smatch_state *tmp_state;
	struct state_list *add_to_one = NULL;
	struct state_list *add_to_two = NULL;

	PREPARE_PTR_LIST(*one, one_state);
	PREPARE_PTR_LIST(*two, two_state);
	for (;;) {
		if (!one_state && !two_state)
			break;
		if (cmp_tracker(one_state, two_state) < 0) {
			tmp_state = __client_unmatched_state_function(one_state);
			tmp = alloc_state_no_name(one_state->name,
						  one_state->owner,
						  one_state->sym, tmp_state);
			add_ptr_list(&add_to_two, tmp);
			NEXT_PTR_LIST(one_state);
		} else if (cmp_tracker(one_state, two_state) == 0) {
			NEXT_PTR_LIST(one_state);
			NEXT_PTR_LIST(two_state);
		} else {
			tmp_state = __client_unmatched_state_function(two_state);
			tmp = alloc_state_no_name(two_state->name,
						  two_state->owner,
						  two_state->sym, tmp_state);
			add_ptr_list(&add_to_one, tmp);
			NEXT_PTR_LIST(two_state);
		}
	}
	FINISH_PTR_LIST(two_state);
	FINISH_PTR_LIST(one_state);

	overwrite_slist(add_to_one, one);
	overwrite_slist(add_to_two, two);
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

	check_order(*to);
	check_order(slist);

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

	match_states(&implied_to, &implied_from);

	PREPARE_PTR_LIST(implied_to, to_state);
	PREPARE_PTR_LIST(implied_from, state);
	for (;;) {
		if (!to_state && !state)
			break;
		if (cmp_tracker(to_state, state) < 0) {
			smatch_msg("error:  Internal smatch error.");
			NEXT_PTR_LIST(to_state);
		} else if (cmp_tracker(to_state, state) == 0) {
			if (to_state != state) {
				add_pool(&to_state->my_pools, implied_to);
				add_pool(&state->my_pools, implied_from);
			}

			tmp = merge_sm_states(to_state, state);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(to_state);
			NEXT_PTR_LIST(state);
		} else {
			smatch_msg("error:  Internal smatch error.");
			NEXT_PTR_LIST(state);
		}
	}
	FINISH_PTR_LIST(state);
	FINISH_PTR_LIST(to_state);

	free_slist(to);
	*to = results;
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
	struct state_list *right_slist = pop_slist(slist_stack);

	FOR_EACH_PTR(right_slist, tmp) {
		overwrite_sm_state_stack(slist_stack, tmp);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&right_slist);
}

/* 
 * or_slist_stack() is for if we have:  if (foo || bar) { foo->baz;
 * It pops the two slists from the top of the stack and merges them
 * together in a way that preserves the things they have in common
 * but creates a merged state for most of the rest.
 * You could have code that had:  if (foo || foo) { foo->baz;
 * It's this function which ensures smatch does the right thing.
 */
void or_slist_stack(struct state_list_stack **pre_conds,
		    struct state_list *cur_slist,
		    struct state_list_stack **slist_stack)
{
	struct state_list *new;
	struct state_list *old;
	struct state_list *res = NULL;
	struct state_list *tmp_slist;

	new = pop_slist(slist_stack);
	old = pop_slist(slist_stack);

	tmp_slist = pop_slist(pre_conds);
	res = clone_slist(tmp_slist);
	push_slist(pre_conds, tmp_slist);
	overwrite_slist(old, &res);

	tmp_slist = clone_slist(cur_slist);
	overwrite_slist(new, &tmp_slist);

	merge_slist(&res, tmp_slist);

	push_slist(slist_stack, res);
	free_slist(&tmp_slist);
	free_slist(&new);
	free_slist(&old);
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

unsigned int __get_allocations()
{
	return sm_state_allocator.allocations;
}
