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
STATE(returned);
STATE(unfree);

static const char *allocation_funcs[] = {
	"malloc",
	"kmalloc",
	NULL,
};

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
		set_state(left_name, my_id, left_sym, &allocated);
		return;
	}

	right_name = get_variable_from_expr(right, &right_sym);
	if (right_name && (state = get_state(right_name, my_id, right_sym))) {
		if (state == &isfree)
			smatch_msg("assigning freed pointer");
		set_state(right_name, my_id, right_sym, &assigned);
	}
	free_string(right_name);

	if (is_freed(left_name, left_sym)) {
		set_state(left_name, my_id, left_sym, &unfree);
	}
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
	state = get_state(name, 3, sym);
	if (state && !strcmp(state->name, "isnull"))
		return 1;
	return 0;
}

static void match_kfree(struct expression *expr)
{
	struct expression *ptr_expr;
	char *fn_name;
	char *ptr_name;
	struct symbol *ptr_sym;


	fn_name = get_variable_from_expr(expr->fn, NULL);

	if (!fn_name || strcmp(fn_name, "kfree"))
		return;

	ptr_expr = get_argument_from_call_expr(expr->args, 0);
	ptr_name = get_variable_from_expr(ptr_expr, &ptr_sym);
	if (is_freed(ptr_name, ptr_sym) && !is_null(ptr_name, ptr_sym)) {
		smatch_msg("double free of %s", ptr_name);
	}
	set_state(ptr_name, my_id, ptr_sym, &isfree);

	free_string(fn_name);
}

static int possibly_allocated(struct state_list *slist)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &allocated)
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
			smatch_msg("possible memery leak of %s", tmp->name);
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
		set_state(name, my_id, sym, &returned);
	}

	check_for_allocated();
}

static void match_function_call(struct expression *expr)
{


}


static void match_end_func(struct symbol *sym)
{
	check_for_allocated();
}

void register_memory(int id)
{
	my_id = id;
	add_hook(&match_kfree, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
