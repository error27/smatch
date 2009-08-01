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
		smatch_msg("warn: '%s' possibly leaked on error path",
			   sm->name);
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
	set_state_expr(my_id, expr->left, &allocated);
free:
	free_string(left_name);
}

static void match_free(const char *fn, struct expression *expr, void *data)
{
	struct expression *ptr_expr;
	long arg_num = PTR_INT(data);

	ptr_expr = get_argument_from_call_expr(expr->args, arg_num);
	if (!get_state_expr(my_id, ptr_expr))
		return;
	set_state_expr(my_id, ptr_expr, &freed);
}

static void check_slist(struct state_list *slist)
{
	struct sm_state *sm;
	struct sm_state *poss;

	FOR_EACH_PTR(slist, sm) {
		if (sm->owner != my_id)
			continue;
		FOR_EACH_PTR(sm->possible, poss) {
			if (poss->state == &allocated)
				smatch_msg("warn: '%s' possibly leaked on error"
					   " path (implied from line %d)",
					   sm->name, poss->line);
		} END_FOR_EACH_PTR(poss);
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

static void set_old_true_false_paths(struct expression *expr)
{
	if (!get_state_expr(my_id, expr))
		return;
	set_true_false_states_expr(my_id, expr, &allocated, &isnull);
}

static void match_condition(struct expression *expr)
{
	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		set_old_true_false_paths(expr);
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

static void register_free_funcs(void)
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
		add_function_hook(func, &match_free, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

static void register_put_funcs(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.puts_argument");
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
		add_function_hook(func, &match_free, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

static void register_allocation_funcs(void)
{
	struct token *token;
	const char *func;

	token = get_tokens_file("kernel.allocation_funcs");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		add_function_assign_hook(func, &match_allocation, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void check_leaks(int id)
{
	int i;

	my_id = id;
	set_default_state(my_id, &undefined);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_function_hook("kfree", &match_free, (void *)0);
	register_free_funcs();
	register_put_funcs();
	for(i = 0; allocation_funcs[i]; i++) {
		add_function_assign_hook(allocation_funcs[i],
					 &match_allocation, NULL);
	}
	register_allocation_funcs();
}
