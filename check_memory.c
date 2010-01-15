/*
 * sparse/check_memory.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <fcntl.h>
#include <unistd.h>
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

static char *get_parent_name(struct symbol *sym)
{
	static char buf[256];

	if (!sym || !sym->ident)
		return NULL;

	snprintf(buf, 255, "-%s", sym->ident->name);
	buf[255] = '\0';
	return alloc_string(buf);
}

static int is_parent_sym(const char *name)
{
	if (!strncmp(name, "-", 1))
		return 1;
	return 0;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (is_parent_sym(sm->name))
		return &assigned;
	return &undefined;
}

static void assign_parent(struct symbol *sym)
{
	char *name;

	name = get_parent_name(sym);
	if (!name)
		return;
	set_state(my_id, name, sym, &assigned);
	free_string(name);
}

static int parent_is_assigned(struct symbol *sym)
{
	struct smatch_state *state;
	char *name;

	name = get_parent_name(sym);
	if (!name)
		return 0;
	state = get_state(my_id, name, sym);
	free_string(name);
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

	slist = get_possible_states(my_id, name, sym);
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
		add_tracker(&arguments, my_id, (arg->ident?arg->ident->name:"NULL"), arg);
	} END_FOR_EACH_PTR(arg);
}

static int is_parent(struct expression *expr)
{
	if (expr->type == EXPR_DEREF)
		return 0;
	return 1;
}

static void match_assign(struct expression *expr)
{
	struct expression *left, *right;
	char *left_name = NULL;
	char *right_name = NULL;
	struct symbol *left_sym, *right_sym;
	struct smatch_state *state;

	left = strip_expr(expr->left);
	left_name = get_variable_from_expr_complex(left, &left_sym);

	right = strip_expr(expr->right);
	if (left_name && left_sym && is_allocation(right) && 
	    !(left_sym->ctype.modifiers & 
	      (MOD_NONLOCAL | MOD_STATIC | MOD_ADDRESSABLE)) &&
	    !parent_is_assigned(left_sym)) {
		set_state(my_id, left_name, left_sym, &malloced);
		goto exit;
	}

	right_name = get_variable_from_expr_complex(right, &right_sym);

	if (right_name && (state = get_state(my_id, right_name, right_sym))) {
		if (state == &isfree)
			sm_msg("error: assigning freed pointer");
		set_state(my_id, right_name, right_sym, &assigned);
	}

	if (is_freed(left_name, left_sym)) {
		set_state(my_id, left_name, left_sym, &unfree);
	}
	if (left_name && is_parent(left))
		assign_parent(left_sym);
	if (right_name && is_parent(right))
		assign_parent(right_sym);
exit:
	free_string(left_name);
	free_string(right_name);
}

static int is_null(const char *name, struct symbol *sym)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (state && !strcmp(state->name, "isnull"))
		return 1;
	return 0;
}

static void set_unfree(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	set_state(my_id, name, sym, &unfree);
}

static void match_free_func(const char *fn, struct expression *expr, void *data)
{
	struct expression *ptr_expr;
	char *ptr_name;
	struct symbol *ptr_sym;
	int arg_num = PTR_INT(data);

	ptr_expr = get_argument_from_call_expr(expr->args, arg_num);
	ptr_name = get_variable_from_expr_complex(ptr_expr, &ptr_sym);
	if (!ptr_name)
		return;
	if (is_freed(ptr_name, ptr_sym) && !is_null(ptr_name, ptr_sym)) {
		sm_msg("error: double free of %s", ptr_name);
	}
	set_state(my_id, ptr_name, ptr_sym, &isfree);
	add_modification_hook(ptr_name, &set_unfree, NULL);
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

static void check_sm_is_leaked(struct sm_state *sm)
{
	if (possibly_allocated(sm->possible) && 
		!is_null(sm->name, sm->sym) &&
		!is_argument(sm->sym) && 
		!parent_is_assigned(sm->sym))
		sm_msg("error: memory leak of %s", sm->name);
}

static void check_tracker_is_leaked(struct tracker *t)
{
	struct sm_state *sm;

	sm = get_sm_state(t->owner, t->name, t->sym);
	if (sm)
		check_sm_is_leaked(sm);
	__free_tracker(t);
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
			set_state(my_id, name, sym, &malloced);
			add_scope_hook((scope_hook *)&check_tracker_is_leaked,
				alloc_tracker(my_id, name, sym));
			scoped_state(my_id, name, sym);
		} else {
			assign_parent(sym);
		}
	}
}

static void check_for_allocated(void)
{
	struct state_list *slist;
	struct sm_state *tmp;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		check_sm_is_leaked(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_return(struct expression *ret_value)
{
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr_complex(ret_value, &sym);
	if (sym)
		assign_parent(sym);
	free_string(name);
	check_for_allocated();
}

static void set_new_true_false_paths(const char *name, struct symbol *sym)
{
	struct smatch_state *tmp;

	tmp = get_state(my_id, name, sym);

	if (!tmp) {
		return;
	}

	if (tmp == &isfree) {
		sm_msg("warn: why do you care about freed memory?");
	}

	if (tmp == &assigned) {
		/* we don't care about assigned pointers any more */
		return;
	}
	set_true_false_states(my_id, name, sym, &allocated, &isnull);
}

static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	switch (expr->type) {
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
	struct sm_state *state;

	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		name = get_variable_from_expr_complex(tmp, &sym);
		if (!name)
			continue;
		if ((state = get_sm_state(my_id, name, sym))) {
			if (possibly_allocated(state->possible)) {
				set_state(my_id, name, sym, &assigned);
			}
		}
		assign_parent(sym);
		free_string(name);
	} END_FOR_EACH_PTR(tmp);
}

static void match_dereferences(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;

	if (expr->type != EXPR_PREOP)
		return;
	expr = strip_expr(expr->unop);

	deref = get_variable_from_expr(expr, &sym);
	if (!deref)
		return;
	if (is_freed(deref, sym)) {
		sm_msg("error: dereferencing freed memory '%s'", deref);
		set_state(my_id, deref, sym, &unfree);
	}
	free_string(deref);
}

static void match_end_func(struct symbol *sym)
{
	check_for_allocated();
	free_trackers_and_list(&arguments);
}

static void register_funcs_from_file(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.frees_argument");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		arg = atoi(token->number);
		add_function_hook(func, &match_free_func, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_memory(int id)
{
	my_id = id;
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	if (option_project == PROJ_KERNEL)
		add_function_hook("kfree", &match_free_func, (void *)0);
	else
		add_function_hook("free", &match_free_func, (void *)0);
	register_funcs_from_file();
}
