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

static struct tracker_list *arguments;

static const char *allocation_funcs[] = {
	"malloc",
	"kmalloc",
	"kzalloc",
	NULL,
};

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (!strcmp(sm->name, "-"))
		return &assigned;
	return &undefined;
}

static void assign_parent(struct symbol *sym)
{
	set_state("-", my_id, sym, &assigned);
}

static int parent_is_assigned(struct symbol *sym)
{
	struct smatch_state *state;

	state = get_state("-", my_id, sym);
	if (state == &assigned)
		return 1;
	return 0;
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

static int is_argument(struct symbol *sym)
{
	struct tracker *arg;

	FOR_EACH_PTR(arguments, arg) {
		if (arg->sym == sym)
			return 1;
	} END_FOR_EACH_PTR(arg);
	return 0;
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		add_tracker(&arguments, (arg->ident?arg->ident->name:"NULL"), my_id, arg);
	} END_FOR_EACH_PTR(arg);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if ((get_base_type(sym))->type == SYM_ARRAY) {
		return;
	}

	name = sym->ident->name;

	if (sym->initializer) {
		if (is_allocation(sym->initializer)) {
			set_state(name, my_id, sym, &malloced);
		} else {
			assign_parent(sym);
		}
	}
}

static int is_parent(struct expression *expr)
{
	if (expr->type == EXPR_DEREF)
		return 0;
	return 1;
}

static int assign_seen;
static int handle_double_assign(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (expr->right->type != EXPR_ASSIGNMENT)
		return 0;
	assign_seen++;
	
	name = get_variable_from_expr_complex(expr->left, &sym);
	if (name && is_parent(expr->left))
		assign_parent(sym);

	name = get_variable_from_expr_complex(expr->right->left, &sym);
	if (name && is_parent(expr->right->left))
		assign_parent(sym);

	name = get_variable_from_expr_complex(expr->right->right, &sym);
	if (name && is_parent(expr->right->right))
		assign_parent(sym);

	return 1;
}

static void match_assign(struct expression *expr)
{
	struct expression *left, *right;
	char *left_name = NULL;
	char *right_name = NULL;
	struct symbol *left_sym, *right_sym;
	struct smatch_state *state;

	if (assign_seen) {
		assign_seen--;
		return;
	}

	if (handle_double_assign(expr)) {
		return;
	}

	left = strip_expr(expr->left);
	left_name = get_variable_from_expr_complex(left, &left_sym);

	right = strip_expr(expr->right);
	if (left_name && left_sym && is_allocation(right) && 
	    !(left_sym->ctype.modifiers & 
	      (MOD_NONLOCAL | MOD_STATIC | MOD_ADDRESSABLE)) &&
	    !parent_is_assigned(left_sym)) {
		set_state(left_name, my_id, left_sym, &malloced);
		goto exit;
	}

	right_name = get_variable_from_expr_complex(right, &right_sym);

	if (right_name && (state = get_state(right_name, my_id, right_sym))) {
		if (state == &isfree)
			smatch_msg("error: assigning freed pointer");
		set_state(right_name, my_id, right_sym, &assigned);
	}

	if (is_freed(left_name, left_sym)) {
		set_state(left_name, my_id, left_sym, &unfree);
	}
	if (left_name && is_parent(left))
		assign_parent(left_sym);
	if (right_name && is_parent(right))
		assign_parent(right_sym);
exit:
	free_string(left_name);
	free_string(right_name);
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
	ptr_name = get_variable_from_expr_complex(ptr_expr, &ptr_sym);
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

static void check_for_allocated(void)
{
	struct state_list *slist;
	struct sm_state *tmp;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (possibly_allocated(tmp->possible) && 
		    !is_null(tmp->name, tmp->sym) &&
		    !is_argument(tmp->sym) && 
		    !parent_is_assigned(tmp->sym))
			smatch_msg("error: memery leak of %s", tmp->name);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_return(struct statement *stmt)
{
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr_complex(stmt->ret_value, &sym);
	if (sym)
		assign_parent(sym);
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

	if (tmp == &assigned) {
		/* we don't care about assigned pointers any more */
		return;
	}
	set_true_false_states(name, my_id, sym, &allocated, &isnull);
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
		name = get_variable_from_expr_complex(expr, &sym);
		if (!name)
			return;
		set_new_true_false_paths(name, sym);
		free_string(name);
		return;
	case EXPR_ASSIGNMENT:
		assign_seen++;
		 /* You have to deal with stuff like if (a = b = c) */
		match_condition(expr->right);
		match_condition(expr->left);
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
		name = get_variable_from_expr_complex(tmp, &sym);
		if (!name)
			continue;
		if ((state = get_sm_state(name, my_id, sym))) {
			if (possibly_allocated(state->possible)) {
				set_state(name, my_id, sym, &assigned);
			}
		}
		assign_parent(sym);
	} END_FOR_EACH_PTR(tmp);
}

static void match_end_func(struct symbol *sym)
{
	check_for_allocated();
	free_trackers_and_list(&arguments);
}

void check_memory(int id)
{
	my_id = id;
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
