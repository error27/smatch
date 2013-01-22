/*
 * sparse/smatch_equiv.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * smatch_equiv.c is for tracking how variables are the same
 *
 * if (a == b) {
 * Or
 * x = y;
 *
 * When a variable gets modified all the old relationships are
 * deleted.  remove_equiv(expr);
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

ALLOCATOR(relation, "related variables");

static struct relation *alloc_relation(const char *name, struct symbol *sym)
{
	struct relation *tmp;

	tmp = __alloc_relation(0);
	tmp->name = alloc_string(name);
	tmp->sym = sym;
	return tmp;
}

struct related_list *clone_related_list(struct related_list *related)
{
	struct relation *rel;
	struct related_list *to_list = NULL;

	FOR_EACH_PTR(related, rel) {
		add_ptr_list(&to_list, rel);
	} END_FOR_EACH_PTR(rel);

	return to_list;
}

static int cmp_relation(struct relation *a, struct relation *b)
{
	int ret;

	if (a == b)
		return 0;

	if (a->sym > b->sym)
		return -1;
	if (a->sym < b->sym)
		return 1;

	ret = strcmp(a->name, b->name);
	if (ret)
		return ret;

	return 0;
}

struct related_list *get_shared_relations(struct related_list *one,
					      struct related_list *two)
{
	struct related_list *ret = NULL;
	struct relation *one_rel;
	struct relation *two_rel;

	PREPARE_PTR_LIST(one, one_rel);
	PREPARE_PTR_LIST(two, two_rel);
	for (;;) {
		if (!one_rel || !two_rel)
			break;
		if (cmp_relation(one_rel, two_rel) < 0) {
			NEXT_PTR_LIST(one_rel);
		} else if (cmp_relation(one_rel, two_rel) == 0) {
			add_ptr_list(&ret, one_rel);
			NEXT_PTR_LIST(one_rel);
			NEXT_PTR_LIST(two_rel);
		} else {
			NEXT_PTR_LIST(two_rel);
		}
	}
	FINISH_PTR_LIST(two_rel);
	FINISH_PTR_LIST(one_rel);

	return ret;
}

static void debug_addition(struct related_list *rlist, const char *name)
{
	struct relation *tmp;

	if (!option_debug_related)
		return;

	sm_prefix();
	sm_printf("(");
	FOR_EACH_PTR(rlist, tmp) {
		sm_printf("%s ", tmp->name);
	} END_FOR_EACH_PTR(tmp);
	sm_printf(") <-- %s\n", name);
}

static void add_related(struct related_list **rlist, const char *name, struct symbol *sym)
{
	struct relation *rel;
	struct relation *new;
	struct relation tmp = {
		.name = (char *)name,
		.sym = sym
	};

	debug_addition(*rlist, name);

	FOR_EACH_PTR(*rlist, rel) {
		if (cmp_relation(rel, &tmp) < 0)
			continue;
		if (cmp_relation(rel, &tmp) == 0)
			return;
		new = alloc_relation(name, sym);
		INSERT_CURRENT(new, rel);
		return;
	} END_FOR_EACH_PTR(rel);
	new = alloc_relation(name, sym);
	add_ptr_list(rlist, new);
}

void del_related(struct smatch_state *state, const char *name, struct symbol *sym)
{
	struct relation *tmp;
	struct relation remove = {
		.name = (char *)name,
		.sym = sym,
	};

	FOR_EACH_PTR(estate_related(state), tmp) {
		if (cmp_relation(tmp, &remove) < 0)
			continue;
		if (cmp_relation(tmp, &remove) == 0) {
			DELETE_CURRENT_PTR(tmp);
			return;
		}
		return;
	} END_FOR_EACH_PTR(tmp);
}

static void del_equiv(struct smatch_state *state, const char *name, struct symbol *sym)
{
	del_related(state, name, sym);
}

void remove_from_equiv(const char *name, struct symbol *sym)
{
	struct sm_state *orig_sm;
	struct relation *rel;
	struct smatch_state *state;
	struct related_list *to_update;

	// FIXME equiv => related
	orig_sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!orig_sm || !get_dinfo(orig_sm->state)->related)
		return;

	state = clone_estate(orig_sm->state);
	del_equiv(state, name, sym);
	to_update = get_dinfo(state)->related;
	if (ptr_list_size((struct ptr_list *)get_dinfo(state)->related) == 1)
		get_dinfo(state)->related = NULL;

	FOR_EACH_PTR(to_update, rel) {
		struct sm_state *new_sm;

		new_sm = clone_sm(orig_sm);
		new_sm->name = rel->name;
		new_sm->sym = rel->sym;
		new_sm->state = state;
		__set_sm(new_sm);
	} END_FOR_EACH_PTR(rel);
}

void remove_from_equiv_expr(struct expression *expr)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	remove_from_equiv(name, sym);
free:
	free_string(name);
}

void set_related(struct smatch_state *estate, struct related_list *rlist)
{
	if (!estate_related(estate) && !rlist)
		return;
	get_dinfo(estate)->related = rlist;
}

/*
 * set_equiv() is only used for assignments where we set one variable
 * equal to the other.  a = b;.  It's not used for if conditions where
 * a == b.
 */
void set_equiv(struct expression *left, struct expression *right)
{
	struct sm_state *right_sm;
	struct smatch_state *state;
	struct relation *rel;
	char *left_name;
	struct symbol *left_sym;
	struct related_list *rlist;

	left_name = expr_to_var_sym(left, &left_sym);
	if (!left_name || !left_sym)
		goto free;

	right_sm = get_sm_state_expr(SMATCH_EXTRA, right);
	if (!right_sm)
		right_sm = set_state_expr(SMATCH_EXTRA, right, alloc_estate_whole(get_type(right)));
	if (!right_sm)
		goto free;

	remove_from_equiv(left_name, left_sym);

	rlist = clone_related_list(estate_related(right_sm->state));
	add_related(&rlist, right_sm->name, right_sm->sym);
	add_related(&rlist, left_name, left_sym);

	state = clone_estate(right_sm->state);
	get_dinfo(state)->related = rlist;

	FOR_EACH_PTR(rlist, rel) {
		struct sm_state *new_sm;

		new_sm = clone_sm(right_sm);
		new_sm->name = rel->name;
		new_sm->sym = rel->sym;
		new_sm->state = state;
		__set_sm(new_sm);
	} END_FOR_EACH_PTR(rel);
free:
	free_string(left_name);
}

void set_equiv_state_expr(int id, struct expression *expr, struct smatch_state *state)
{
	struct relation *rel;
	struct smatch_state *estate;

	estate = get_state_expr(SMATCH_EXTRA, expr);

	if (!estate)
		return;

	FOR_EACH_PTR(get_dinfo(estate)->related, rel) {
		set_state(id, rel->name, rel->sym, state);
	} END_FOR_EACH_PTR(rel);
}
