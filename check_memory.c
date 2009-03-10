/*
 * sparse/check_memory.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(allocated);
STATE(assigned);
STATE(isfree);
STATE(malloced);
STATE(isnull);
STATE(unfree);

/*
  malloced --> allocated --> assigned --> isfree +
            \-> isnull.    \-> isfree +

  isfree --> unfree.
          \-> isnull.
*/

/* If we pass a parent to a function that sets all the 
   children to assigned.  frob(x) means x->data is assigned. */
struct parent {
	struct symbol *sym;
	struct tracker_list *children;
};
ALLOCATOR(parent, "parents");
DECLARE_PTR_LIST(parent_list, struct parent);
static struct parent_list *parents;

static const char *allocation_funcs[] = {
	"malloc",
	"kmalloc",
	NULL,
};

static void add_parent_to_parents(char *name, struct symbol *sym)
{
	struct parent *tmp;

	FOR_EACH_PTR(parents, tmp) {
		if (tmp->sym == sym) {
			add_tracker(&tmp->children, name, my_id, sym);
			return;
		}
	} END_FOR_EACH_PTR(tmp);

	tmp = __alloc_parent(0);
	tmp->sym = sym;
	tmp->children = NULL;
	add_tracker(&tmp->children, name, my_id, sym);
	add_ptr_list(&parents, tmp);
}

static void set_list_assigned(struct tracker_list *children)
{
	struct tracker *child;

	FOR_EACH_PTR(children, child) {
		set_state(child->name, my_id, child->sym, &assigned);
	} END_FOR_EACH_PTR(child);
}

static struct tracker_list *get_children(struct symbol *sym)
{
	struct parent *tmp;

	FOR_EACH_PTR(parents, tmp) {
		if (tmp->sym == sym) {
			return tmp->children;
		}
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

static void set_children_assigned(struct symbol *sym)
{
	struct tracker_list *children;

	if ((children = get_children(sym))) {
		set_list_assigned(children);
	}
}

static int is_allocation(struct expression *expr)
{
	char *fn_name;
	int i;

	if (expr->type != EXPR_CALL)
		return 0;

	if (!(fn_name = get_variable_from_expr(expr->fn, NULL)))
		return 0;

	for (i = 0; allocation_funcs[i]; i++) {
		if (!strcmp(fn_name, allocation_funcs[i])) {
			free_string(fn_name);
			return 1;
		}
	}
	free_string(fn_name);
	return 0;
}

static int is_freed(const char *name, struct symbol *sym)
{
	struct state_list *slist;

	slist = get_possible_states(name, my_id, sym);
	if (slist_has_state(slist, &isfree)) {
		return 1;
	}
	return 0;
}

static void match_assign(struct expression *expr)
{
	struct expression *left, *right;
	char *left_name, *right_name;
	struct symbol *left_sym, *right_sym;
	struct smatch_state *state;

	left = strip_expr(expr->left);
	left_name = get_variable_from_expr(left, &left_sym);
	if (!left_name)
		return;
	if (!left_sym) {
		free_string(left_name);
		return;
	}

	right = strip_expr(expr->right);
	if (is_allocation(right) && !(left_sym->ctype.modifiers &
			(MOD_NONLOCAL | MOD_STATIC | MOD_ADDRESSABLE))) {
		set_state(left_name, my_id, left_sym, &malloced);
		add_parent_to_parents(left_name, left_sym);
		free_string(left_name);
		return;
	}

	right_name = get_variable_from_expr(right, &right_sym);
	if (right_name && (state = get_state(right_name, my_id, right_sym))) {
		if (state == &isfree)
			smatch_msg("error: assigning freed pointer");
		set_state(right_name, my_id, right_sym, &assigned);
	}
	free_string(right_name);

	if (is_freed(left_name, left_sym)) {
		set_state(left_name, my_id, left_sym, &unfree);
	}
	free_string(left_name);
}

static int is_null(char *name, struct symbol *sym)
{
	struct smatch_state *state;

	/* 
	 * FIXME.  Ha ha ha... This is so wrong.
	 * I'm pulling in data from the check_null_deref script.  
	 * I just happen to know that its ID is 3.
	 * The correct approved way to do this is to get the data from
	 * smatch_extra.  But right now smatch_extra doesn't track it.
	 */
	state = get_state(name, my_id, sym);
	if (state && !strcmp(state->name, "isnull"))
		return 1;
	return 0;
}

static void match_kfree(struct expression *expr)
{
	struct expression *ptr_expr;
	char *ptr_name;
	struct symbol *ptr_sym;

	ptr_expr = get_argument_from_call_expr(expr->args, 0);
	ptr_name = get_variable_from_expr(ptr_expr, &ptr_sym);
	if (is_freed(ptr_name, ptr_sym) && !is_null(ptr_name, ptr_sym)) {
		smatch_msg("error: double free of %s", ptr_name);
	}
	set_state(ptr_name, my_id, ptr_sym, &isfree);
	free_string(ptr_name);
}

static int possibly_allocated(struct state_list *slist)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &allocated)
			return 1;
		if (tmp->state == &malloced)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void check_for_allocated()
{
	struct state_list *slist;
	struct sm_state *tmp;

	return;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (possibly_allocated(tmp->possible) && 
			!is_null(tmp->name, tmp->sym))
			smatch_msg("error: memery leak of %s", tmp->name);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_return(struct statement *stmt)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;

	name = get_variable_from_expr(stmt->ret_value, &sym);
	if ((state = get_state(name, my_id, sym))) {
		set_state(name, my_id, sym, &assigned);
	}
	free_string(name);
	check_for_allocated();
}

static void set_new_true_false_paths(const char *name, struct symbol *sym)
{
	struct smatch_state *tmp;

	tmp = get_state(name, my_id, sym);

	if (!tmp) {
		return;
	}

	if (tmp == &isfree) {
		smatch_msg("warn: why do you care about freed memory?");
	}

	if (tmp == &malloced) {
		set_true_false_states(name, my_id, sym, &allocated, &isnull);
	}
}

static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		name = get_variable_from_expr(expr, &sym);
		if (!name)
			return;
		set_new_true_false_paths(name, sym);
		free_string(name);
		return;
	default:
		return;
	}
}

static void match_function_call(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	char *fn_name;
	struct sm_state *state;

	fn_name = get_variable_from_expr(expr->fn, NULL);

	if (fn_name && !strcmp(fn_name, "kfree")) {
		match_kfree(expr);
	}

	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		name = get_variable_from_expr(tmp, &sym);
		if (!name)
			continue;
		if ((state = get_sm_state(name, my_id, sym))) {
			if (possibly_allocated(state->possible)) {
				set_state(name, my_id, sym, &assigned);
			}
		}
		set_children_assigned(sym);
		/* get parent.  set children to assigned */
	} END_FOR_EACH_PTR(tmp);
}

static void free_the_parents()
{
	struct parent *tmp;

	FOR_EACH_PTR(parents, tmp) {
		free_trackers_and_list(&tmp->children);
	} END_FOR_EACH_PTR(tmp);
	__free_ptr_list((struct ptr_list **)&parents);
}

static void match_end_func(struct symbol *sym)
{
	check_for_allocated();
	free_the_parents();
}

void register_memory(int id)
{
	my_id = id;
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
