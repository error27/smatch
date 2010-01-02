/*
 * sparse/check_frees_argument.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/* 
 * This script is for finding functions like hcd_buffer_free() which free
 * their arguments.  After running it, add those functions to check_memory.c
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(freed);

static struct symbol *this_func;
static struct tracker_list *freed_args = NULL;

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

static int is_arg(char *name, struct symbol *sym)
{
	struct symbol *arg;
	const char *arg_name;

	FOR_EACH_PTR(this_func->ctype.base_type->arguments, arg) {
		arg_name = (arg->ident?arg->ident->name:"-");
		if (sym == arg && !strcmp(name, arg_name))
			return 1;
	} END_FOR_EACH_PTR(arg);
	return 0;
}

static void match_kfree(const char *fn, struct expression *expr, void *info)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;

	tmp = get_argument_from_call_expr(expr->args, 0);
	tmp = strip_expr(tmp);
	name = get_variable_from_expr(tmp, &sym);
	if (is_arg(name, sym)) {
		set_state(my_id, name, sym, &freed);
	}
	free_string(name);
}

static int return_count = 0;
static void match_return(struct expression *ret_value)
{
	struct state_list *slist;
	struct sm_state *tmp;
	struct tracker *tracker;

	if (!return_count) {
		slist = get_all_states(my_id);
		FOR_EACH_PTR(slist, tmp) {
			if (tmp->state == &freed)
				add_tracker(&freed_args, my_id, tmp->name, 
					    tmp->sym);
		} END_FOR_EACH_PTR(tmp);
		free_slist(&slist);
	} else {
		FOR_EACH_PTR(freed_args, tracker) {
			tmp = get_sm_state(my_id, tracker->name, tracker->sym);
			if (tmp && tmp->state != &freed)
				del_tracker(&freed_args, my_id, tracker->name, 
					    tracker->sym);
		} END_FOR_EACH_PTR(tracker);
		
	}
}

static void print_arg(struct symbol *sym)
{
	struct symbol *arg;
	int i = 0;

	FOR_EACH_PTR(this_func->ctype.base_type->arguments, arg) {
		if (sym == arg) {
			sm_info("free_arg %s %d", get_function(), i);
			return;
		}
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void match_end_func(struct symbol *sym)
{
	struct tracker *tracker;

	if (is_reachable())
		match_return(NULL);

	FOR_EACH_PTR(freed_args, tracker) {
		print_arg(tracker->sym);
	} END_FOR_EACH_PTR(tracker);

	free_trackers_and_list(&freed_args);
	return_count = 0;
}

void check_frees_argument(int id)
{
	if (!option_spammy)
		return;

	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	if (option_project == PROJ_KERNEL)
		add_function_hook("kfree", &match_kfree, NULL);
	else
		add_function_hook("free", &match_kfree, NULL);
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
