/*
 * smatch/check_release_resource.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * I found a bug where someone released the wrong resource and wanted to 
 * prevent that from happening again.
 *
 */

#include "smatch.h"

static int my_id;

static struct tracker_list *resource_list;

static void match_request(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *arg_expr;
	int arg_no = (int)_arg_no;
	char *name;
	struct symbol *sym;

	arg_expr = get_argument_from_call_expr(expr->args, arg_no);
	arg_expr = strip_expr(arg_expr);

	name = get_variable_from_expr(arg_expr, &sym);
	if (!name || !sym)
		goto free;
	add_tracker(&resource_list, my_id, name, sym);
free:
	free_string(name);
}

static void match_release(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *arg_expr;
	int arg_no = (int)_arg_no;
	char *name;
	struct symbol *sym;

	arg_expr = get_argument_from_call_expr(expr->args, arg_no);
	arg_expr = strip_expr(arg_expr);

	if (!resource_list)
		return;

	name = get_variable_from_expr(arg_expr, &sym);
	if (!name || !sym)
		goto free;
	if (in_tracker_list(resource_list, my_id, name, sym))
		goto free;
	sm_msg("warn: '%s' was not one of the resources you requested", name);
free:
	free_string(name);
}

static void match_end_func(struct symbol *sym)
{
	free_trackers_and_list(&resource_list);
}

void check_release_resource(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_hook("request_resource", &match_request, (void *)0);
	add_function_hook("release_resource", &match_release, (void *)0);
	add_function_hook("request_mem_resource", &match_request, (void *)0);
	add_function_hook("release_mem_resource", &match_release, (void *)0);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
