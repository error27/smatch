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
STATE(isnull);
STATE(freed);

/*
  The previous memory leak script found about 3 leaks.  Almost all the leaks
  it found were real though so that has to be said in it's favour.

  The goal of this check is to find a lot more.

  On this check, the plan is to look at return values.  If the value is
  negative we print an error for every local variable that is not freed.
*/

static struct tracker_list *arguments;

static const char *allocation_funcs[] = {
	"malloc",
	"kmalloc",
	"kzalloc",
	NULL,
};

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

static void check_allocated(struct sm_state *sm)
{
	if (slist_has_state(sm->possible, &allocated))
		smatch_msg("warn: %s possibly leaked on error path", sm->name);
}

static void check_for_allocated(void)
{
	struct state_list *slist;
	struct sm_state *tmp;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		check_allocated(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_allocation(const char *fn, struct expression *expr,
			     void *info)
{
	char *left_name = NULL;
	struct symbol *left_sym;

	left_name = get_variable_from_expr(expr->left, &left_sym);
	if (!left_name || !left_sym)
		goto free;
	if (is_argument(left_sym))
		goto free;
	if (left_sym->ctype.modifiers & 
	    (MOD_NONLOCAL | MOD_STATIC | MOD_ADDRESSABLE))
		goto free;
	set_state(left_name, my_id, left_sym, &allocated);
free:
	free_string(left_name);
}

static void match_free(const char *fn, struct expression *expr, void *data)
{
	struct expression *ptr_expr;
	char *ptr_name;
	struct symbol *ptr_sym;
	int arg_num = (int)data;

	ptr_expr = get_argument_from_call_expr(expr->args, arg_num);
	ptr_name = get_variable_from_expr(ptr_expr, &ptr_sym);
	if (!ptr_name)
		return;
	if (!get_state(ptr_name, my_id, ptr_sym))
		return;
	set_state(ptr_name, my_id, ptr_sym, &freed);
	free_string(ptr_name);
}

static void check_slist(struct state_list *slist)
{
	struct sm_state *sm;

	FOR_EACH_PTR(slist, sm) {
		if (sm->owner != my_id)
			continue;
		if (slist_has_state(sm->possible, &allocated))
			smatch_msg("warn: %s possibly leaked on error path (implied)",
				   sm->name);
	} END_FOR_EACH_PTR(sm);
}

static void do_implication_check(struct expression *expr)
{
	struct state_list *lt_zero = NULL;
	struct state_list *ge_zero = NULL;
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	get_implications(name, sym, '<', 0, &lt_zero, &ge_zero);
	check_slist(lt_zero);
free:
	free_slist(&ge_zero);
	free_slist(&lt_zero);
	free_string(name);
}

static void match_return(struct statement *stmt)
{
	int ret_val;

	ret_val = get_value(stmt->ret_value);
	if (ret_val == UNDEFINED) {
		do_implication_check(stmt->ret_value);
		return;
	}
	if (ret_val >= 0)
		return;
	check_for_allocated();
}

static void set_new_true_false_paths(const char *name, struct symbol *sym)
{
	if (!get_state(name, my_id, sym))
		return;
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
		name = get_variable_from_expr(expr, &sym);
		if (!name)
			return;
		set_new_true_false_paths(name, sym);
		free_string(name);
		return;
	case EXPR_ASSIGNMENT:
		match_condition(expr->left);
		return;
	default:
		return;
	}
}

static void match_end_func(struct symbol *sym)
{
	free_trackers_and_list(&arguments);
}

static void register_funcs_from_file(void)
{
	const char *filename = "frees";
	int fd;
	struct token *token;
	const char *func;
	int arg;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return;
	token = tokenize(filename, fd, NULL, NULL);
	close(fd);
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
		add_function_hook(func, &match_free, (void *)arg);
		token = token->next;
	}
	clear_token_alloc();
}

void check_leaks(int id)
{
	int i;

	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_function_hook("kfree", &match_free, (void *)0);
	register_funcs_from_file();
	for(i = 0; allocation_funcs[i]; i++) {
		add_function_assign_hook(allocation_funcs[i],
					 &match_allocation, NULL);
	}
}
