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

#undef CHECKORDER

ALLOCATOR(smatch_state, "smatch state");
ALLOCATOR(sm_state, "sm state");
ALLOCATOR(named_slist, "named slist");
__DO_ALLOCATOR(char, 1, 4, "state names", sname);

static int sm_state_counter;

char *show_sm(struct sm_state *sm)
{
	static char buf[256];
	struct sm_state *tmp;
	int pos;
	int i;

	pos = snprintf(buf, sizeof(buf), "[%s] '%s' = %s (",
		       check_name(sm->owner), sm->name, show_state(sm->state));
	if (pos > sizeof(buf))
		goto truncate;

	i = 0;
	FOR_EACH_PTR(sm->possible, tmp) {
		if (i++)
			pos += snprintf(buf + pos, sizeof(buf) - pos, ", ");
		if (pos > sizeof(buf))
			goto truncate;
		pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
			       show_state(tmp->state));
		if (pos > sizeof(buf))
			goto truncate;
	} END_FOR_EACH_PTR(tmp);
	snprintf(buf + pos, sizeof(buf) - pos, ")");

	return buf;

truncate:
	for (i = 0; i < 3; i++)
		buf[sizeof(buf) - 2 - i] = '.';
	return buf;
}

void __print_slist(struct state_list *slist)
{
	struct sm_state *sm;

	printf("dumping slist at %d\n", get_lineno());
	FOR_EACH_PTR(slist, sm) {
		printf("%s\n", show_sm(sm));
	} END_FOR_EACH_PTR(sm);
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

static struct sm_state *alloc_sm_state(int owner, const char *name,
			     struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *sm_state = __alloc_sm_state(0);

	sm_state_counter++;

	sm_state->name = alloc_sname(name);
	sm_state->owner = owner;
	sm_state->sym = sym;
	sm_state->state = state;
	sm_state->line = get_lineno();
	sm_state->merged = 0;
	sm_state->implied = 0;
	sm_state->pool = NULL;
	sm_state->left = NULL;
	sm_state->right = NULL;
	sm_state->nr_children = 1;
	sm_state->possible = NULL;
	add_ptr_list(&sm_state->possible, sm_state);
	return sm_state;
}

static struct sm_state *alloc_state_no_name(int owner, const char *name,
				     struct symbol *sym,
				     struct smatch_state *state)
{
	struct sm_state *tmp;

	tmp = alloc_sm_state(owner, NULL, sym, state);
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

static void copy_possibles(struct sm_state *to, struct sm_state *from)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(from->possible, tmp) {
		add_sm_state_slist(&to->possible, tmp);
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

int out_of_memory()
{
	/*
	 * I decided to use 50M here based on trial and error.
	 * It works out OK for the kernel and so it should work
	 * for most other projects as well.
	 */
	if (sm_state_counter * sizeof(struct sm_state) >= 50000000)
		return 1;
	return 0;
}

int low_on_memory(void)
{
	if (sm_state_counter * sizeof(struct sm_state) >= 25000000)
		return 1;
	return 0;
}

static void free_sm_state(struct sm_state *sm)
{
	free_slist(&sm->possible);
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

	sm_state_counter = 0;
}

struct sm_state *clone_sm(struct sm_state *s)
{
	struct sm_state *ret;

	ret = alloc_state_no_name(s->owner, s->name, s->sym, s->state);
	ret->merged = s->merged;
	ret->implied = s->implied;
	ret->line = s->line;
	/* clone_sm() doesn't copy the pools.  Each state needs to have
	   only one pool. */
	ret->possible = clone_slist(s->possible);
	ret->left = s->left;
	ret->right = s->right;
	ret->nr_children = s->nr_children;
	return ret;
}

int is_merged(struct sm_state *sm)
{
	return sm->merged;
}

int is_implied(struct sm_state *sm)
{
	return sm->implied;
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
	struct sm_state *sm;
	struct sm_state *last = NULL;
	int printed = 0;

	FOR_EACH_PTR(slist, sm) {
		if (last && cmp_tracker(sm, last) <= 0) {
			printf("Error.  Unsorted slist %d vs %d, %p vs %p, "
			       "%s vs %s\n", last->owner, sm->owner,
			       last->sym, sm->sym, last->name, sm->name);
			printed = 1;
		}
		last = sm;
	} END_FOR_EACH_PTR(sm);

	if (printed)
		printf("======\n");
#endif
}

struct state_list *clone_slist(struct state_list *from_slist)
{
	struct sm_state *sm;
	struct state_list *to_slist = NULL;

	FOR_EACH_PTR(from_slist, sm) {
		add_ptr_list(&to_slist, sm);
	} END_FOR_EACH_PTR(sm);
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

struct smatch_state *merge_states(int owner, const char *name,
				  struct symbol *sym,
				  struct smatch_state *state1,
				  struct smatch_state *state2)
{
	struct smatch_state *ret;

	if (state1 == state2)
		ret = state1;
	else if (__has_merge_function(owner))
		ret = __client_merge_function(owner, state1, state2);
	else if (!state1 || !state2)
		ret = &undefined;
	else
		ret = &merged;
	return ret;
}

struct sm_state *merge_sm_states(struct sm_state *one, struct sm_state *two)
{
	struct smatch_state *s;
	struct sm_state *result;

	if (one == two)
		return one;
	s = merge_states(one->owner, one->name, one->sym, one->state, two->state);
	result = alloc_state_no_name(one->owner, one->name, one->sym, s);
	result->merged = 1;
	result->left = one;
	result->right = two;
	result->nr_children = one->nr_children + two->nr_children;
	copy_possibles(result, one);
	copy_possibles(result, two);

	if (option_debug) {
		struct sm_state *tmp;
		int i = 0;

		printf("%d merge [%s] '%s' %s(L %d) + %s(L %d) => %s (",
			get_lineno(), check_name(one->owner), one->name,
			show_state(one->state), one->line,
			show_state(two->state), two->line,
			show_state(s));

		FOR_EACH_PTR(result->possible, tmp) {
			if (i++)
				printf(", ");
			printf("%s", show_state(tmp->state));
		} END_FOR_EACH_PTR(tmp);
		printf(")\n");
	}

	return result;
}

struct sm_state *get_sm_state_slist(struct state_list *slist, int owner, const char *name,
				struct symbol *sym)
{
	struct sm_state *sm;

	if (!name)
		return NULL;

	FOR_EACH_PTR(slist, sm) {
		if (sm->owner == owner && sm->sym == sym && !strcmp(sm->name, name))
			return sm;
	} END_FOR_EACH_PTR(sm);
	return NULL;
}

struct smatch_state *get_state_slist(struct state_list *slist,
				int owner, const char *name,
				struct symbol *sym)
{
	struct sm_state *sm;

	sm = get_sm_state_slist(slist, owner, name, sym);
	if (sm)
		return sm->state;
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
			struct sm_state *sm)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	overwrite_sm_state(&slist, sm);
	push_slist(stack, slist);
}

struct sm_state *set_state_slist(struct state_list **slist, int owner, const char *name,
		     struct symbol *sym, struct smatch_state *state)
{
	struct sm_state *tmp;
	struct sm_state *new = alloc_sm_state(owner, name, sym, state);

	FOR_EACH_PTR(*slist, tmp) {
		if (cmp_tracker(tmp, new) < 0)
			continue;
		else if (cmp_tracker(tmp, new) == 0) {
			REPLACE_CURRENT_PTR(tmp, new);
			return new;
		} else {
			INSERT_CURRENT(new, tmp);
			return new;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(slist, new);
	return new;
}

void delete_state_slist(struct state_list **slist, int owner, const char *name,
			struct symbol *sym)
{
	struct sm_state *sm;

	FOR_EACH_PTR(*slist, sm) {
		if (sm->owner == owner && sm->sym == sym && !strcmp(sm->name, name)) {
			DELETE_CURRENT_PTR(sm);
			return;
		}
	} END_FOR_EACH_PTR(sm);
}

void delete_state_stack(struct state_list_stack **stack, int owner, const char *name,
			struct symbol *sym)
{
	struct state_list *slist;

	slist = pop_slist(stack);
	delete_state_slist(&slist, owner, name, sym);
	push_slist(stack, slist);
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
struct sm_state *set_state_stack(struct state_list_stack **stack, int owner, const char *name,
				struct symbol *sym, struct smatch_state *state)
{
	struct state_list *slist;
	struct sm_state *sm;

	slist = pop_slist(stack);
	sm = set_state_slist(&slist, owner, name, sym, state);
	push_slist(stack, slist);

	return sm;
}

/*
 * get_sm_state_stack() gets the state for the top slist on the stack.
 */
struct sm_state *get_sm_state_stack(struct state_list_stack *stack,
				int owner, const char *name,
				struct symbol *sym)
{
	struct state_list *slist;
	struct sm_state *ret;

	slist = pop_slist(&stack);
	ret = get_sm_state_slist(slist, owner, name, sym);
	push_slist(&stack, slist);
	return ret;
}

struct smatch_state *get_state_stack(struct state_list_stack *stack,
				int owner, const char *name,
				struct symbol *sym)
{
	struct sm_state *sm;

	sm = get_sm_state_stack(stack, owner, name, sym);
	if (sm)
		return sm->state;
	return NULL;
}

static void match_states(struct state_list **one, struct state_list **two)
{
	struct sm_state *one_sm;
	struct sm_state *two_sm;
	struct sm_state *tmp;
	struct smatch_state *tmp_state;
	struct state_list *add_to_one = NULL;
	struct state_list *add_to_two = NULL;

	PREPARE_PTR_LIST(*one, one_sm);
	PREPARE_PTR_LIST(*two, two_sm);
	for (;;) {
		if (!one_sm && !two_sm)
			break;
		if (cmp_tracker(one_sm, two_sm) < 0) {
			tmp_state = __client_unmatched_state_function(one_sm);
			tmp = alloc_state_no_name(one_sm->owner, one_sm->name,
						  one_sm->sym, tmp_state);
			add_ptr_list(&add_to_two, tmp);
			NEXT_PTR_LIST(one_sm);
		} else if (cmp_tracker(one_sm, two_sm) == 0) {
			NEXT_PTR_LIST(one_sm);
			NEXT_PTR_LIST(two_sm);
		} else {
			tmp_state = __client_unmatched_state_function(two_sm);
			tmp = alloc_state_no_name(two_sm->owner, two_sm->name,
						  two_sm->sym, tmp_state);
			add_ptr_list(&add_to_one, tmp);
			NEXT_PTR_LIST(two_sm);
		}
	}
	FINISH_PTR_LIST(two_sm);
	FINISH_PTR_LIST(one_sm);

	overwrite_slist(add_to_one, one);
	overwrite_slist(add_to_two, two);
}

static void clone_pool_havers(struct state_list *slist)
{
	struct sm_state *sm;
	struct sm_state *new;

	FOR_EACH_PTR(slist, sm) {
		if (sm->pool) {
			new = clone_sm(sm);
			REPLACE_CURRENT_PTR(sm, new);
		}
	} END_FOR_EACH_PTR(sm);
}

int __slist_id;
/*
 * Sets the first state to the slist_id.
 */
static void set_slist_id(struct state_list *slist)
{
	struct smatch_state *state;
	struct sm_state *tmp, *new;

	state = alloc_state_num(++__slist_id);
	new = alloc_sm_state(-1, "unnull_path", NULL, state);

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->owner != (unsigned short)-1)
			return;
		REPLACE_CURRENT_PTR(tmp, new);
		return;
	} END_FOR_EACH_PTR(tmp);
}

int get_slist_id(struct state_list *slist)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->owner != (unsigned short)-1)
			return 0;
		return PTR_INT(tmp->state->data);
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

/*
 * merge_slist() is called whenever paths merge, such as after
 * an if statement.  It takes the two slists and creates one.
 */
void merge_slist(struct state_list **to, struct state_list *slist)
{
	struct sm_state *one_sm, *two_sm, *tmp;
	struct state_list *results = NULL;
	struct state_list *implied_one = NULL;
	struct state_list *implied_two = NULL;

	if (out_of_memory())
		return;

	check_order(*to);
	check_order(slist);

	/* merging a null and nonnull path gives you only the nonnull path */
	if (!slist)
		return;

	if (!*to) {
		*to = clone_slist(slist);
		return;
	}

	implied_one = clone_slist(*to);
	implied_two = clone_slist(slist);

	match_states(&implied_one, &implied_two);

	clone_pool_havers(implied_one);
	clone_pool_havers(implied_two);

	set_slist_id(implied_one);
	set_slist_id(implied_two);

	PREPARE_PTR_LIST(implied_one, one_sm);
	PREPARE_PTR_LIST(implied_two, two_sm);
	for (;;) {
		if (!one_sm && !two_sm)
			break;
		if (cmp_tracker(one_sm, two_sm) < 0) {
			sm_msg("error:  Internal smatch error.");
			NEXT_PTR_LIST(one_sm);
		} else if (cmp_tracker(one_sm, two_sm) == 0) {
			if (one_sm != two_sm) {
				one_sm->pool = implied_one;
				two_sm->pool = implied_two;
			}

			tmp = merge_sm_states(one_sm, two_sm);
			add_ptr_list(&results, tmp);
			NEXT_PTR_LIST(one_sm);
			NEXT_PTR_LIST(two_sm);
		} else {
			sm_msg("error:  Internal smatch error.");
			NEXT_PTR_LIST(two_sm);
		}
	}
	FINISH_PTR_LIST(two_sm);
	FINISH_PTR_LIST(one_sm);

	free_slist(to);
	*to = results;
}

/*
 * filter_slist() removes any sm states "slist" holds in common with "filter"
 */
void filter_slist(struct state_list **slist, struct state_list *filter)
{
	struct sm_state *one_sm, *two_sm;
	struct state_list *results = NULL;

	PREPARE_PTR_LIST(*slist, one_sm);
	PREPARE_PTR_LIST(filter, two_sm);
	for (;;) {
		if (!one_sm && !two_sm)
			break;
		if (cmp_tracker(one_sm, two_sm) < 0) {
			add_ptr_list(&results, one_sm);
			NEXT_PTR_LIST(one_sm);
		} else if (cmp_tracker(one_sm, two_sm) == 0) {
			if (one_sm != two_sm)
				add_ptr_list(&results, one_sm);
			NEXT_PTR_LIST(one_sm);
			NEXT_PTR_LIST(two_sm);
		} else {
			NEXT_PTR_LIST(two_sm);
		}
	}
	FINISH_PTR_LIST(two_sm);
	FINISH_PTR_LIST(one_sm);

	free_slist(slist);
	*slist = results;
}

/*
 * and_slist_stack() pops the top two slists, overwriting the one with
 * the other and pushing it back on the stack.
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
	struct state_list *pre_slist;
	struct state_list *res;
	struct state_list *tmp_slist;

	new = pop_slist(slist_stack);
	old = pop_slist(slist_stack);

	pre_slist = pop_slist(pre_conds);
	push_slist(pre_conds, clone_slist(pre_slist));

	res = clone_slist(pre_slist);
	overwrite_slist(old, &res);

	tmp_slist = clone_slist(cur_slist);
	overwrite_slist(new, &tmp_slist);

	merge_slist(&res, tmp_slist);
	filter_slist(&res, pre_slist);

	push_slist(slist_stack, res);
	free_slist(&tmp_slist);
	free_slist(&pre_slist);
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
