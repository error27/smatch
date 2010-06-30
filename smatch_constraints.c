/*
 * sparse/smatch_constraints.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * smatch_constraints.c is for tracking how variables are related
 *
 * if (a == b) {
 * if (a > b) {
 * if (a != b) {
 *
 * This is stored in a field in the smatch_extra dinfo.
 *
 * Normally the way that variables become related is through a 
 * condition and you say:  add_constraint_expr(left, '<', right);
 * The other way it can happen is if you have an assignment:
 * set_equiv(left, right);
 *
 * One two variables "a" and "b" are related if then if we find
 * that "a" is greater than 0 we need to update "b".
 *
 * When a variable gets modified all the old relationships are
 * deleted.  remove_contraints(expr);
 *
 * Also we need an is_true_constraint(left, '<', right) and 
 * is_false_constraint (left, '<', right).  This is used by 
 * smatch_implied.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

void add_equiv(struct smatch_state *state, const char *name, struct symbol *sym)
{
	struct data_info *dinfo;

	dinfo = get_dinfo(state);
	add_tracker(&dinfo->equiv, SMATCH_EXTRA, name, sym);
}

static void del_equiv(struct smatch_state *state, const char *name, struct symbol *sym)
{
	struct data_info *dinfo;

	dinfo = get_dinfo(state);
	del_tracker(&dinfo->equiv, SMATCH_EXTRA, name, sym);
}

void remove_from_equiv(const char *name, struct symbol *sym)
{
	struct sm_state *orig_sm;
	struct tracker *tracker;
	struct smatch_state *state;
	struct tracker_list *to_update;

	orig_sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!orig_sm || !get_dinfo(orig_sm->state)->equiv)
		return;

	state = clone_extra_state(orig_sm->state);
	del_equiv(state, name, sym);
	to_update = get_dinfo(state)->equiv;
	if (ptr_list_size((struct ptr_list *)get_dinfo(state)->equiv) == 1)
		get_dinfo(state)->equiv = NULL;

	FOR_EACH_PTR(to_update, tracker) {
		struct sm_state *new_sm;

		new_sm = clone_sm(orig_sm);
		new_sm->name = tracker->name;
		new_sm->sym = tracker->sym;
		new_sm->state = state;
		__set_sm(new_sm);
	} END_FOR_EACH_PTR(tracker);
}

void remove_from_equiv_expr(struct expression *expr)
{
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	remove_from_equiv(name, sym);
free:
	free_string(name);
}

void add_constrain_expr(struct expression *left, int op, struct expression *right)
{

}

