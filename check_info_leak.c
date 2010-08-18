/*
 * smatch/check_info_leak.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(alloced);
STATE(string);

static char *my_get_variable(struct expression *expr, struct symbol **sym)
{
	char *name;

	name = get_variable_from_expr(expr, sym);
	free_string(name);
	if (!name || !*sym)
		return NULL;

	return (*sym)->ident->name;
}

static void match_kmalloc(const char *fn, struct expression *expr, void *unused)
{
	char *name;
	struct symbol *sym;

	name = my_get_variable(expr->left, &sym);
	if (!name)
		return;
	set_state(my_id, name, sym, &alloced);
}

static void match_strcpy(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest;
	char *name;
	struct symbol *sym;

	dest = get_argument_from_call_expr(expr->args, 0);
	name = my_get_variable(dest, &sym);
	if (!name || !sym)
		return;
	if (!get_state(my_id, name, sym))
		return;
	set_state(my_id, name, sym, &string);
}

static void match_copy_to_user(const char *fn, struct expression *expr, void *unused)
{
	struct expression *src;
	char *name;
	struct symbol *sym;
	struct sm_state *sm;

	src = get_argument_from_call_expr(expr->args, 1);
	name = my_get_variable(src, &sym);
	if (!name || !sym)
		return;
	sm = get_sm_state(my_id, name, sym);
	if (!sm || !slist_has_state(sm->possible, &string))
		return;
	name = get_variable_from_expr(src, NULL);
	sm_msg("warn: possible info leak '%s'", name);
	free_string(name);
}

void check_info_leak(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	add_function_assign_hook("kmalloc", &match_kmalloc, NULL);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strlcpy", &match_strcpy, NULL);
	add_function_hook("strlcat", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_strcpy, NULL);
	add_function_hook("copy_to_user", &match_copy_to_user, NULL);
}
