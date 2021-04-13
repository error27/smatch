/*
 * Copyright (C) 2020 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

/*
 * This is the "strict" version which is more daring and ambitious than
 * the check_free.c file.  The difference is that this looks at split
 * returns and the other only looks at if every path frees a parameter.
 * Also this has a bunch of kernel specific things to do with reference
 * counted memory.
 */

#include <string.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
static int next_id;

STATE(added);
STATE(trigger);

static unsigned long silenced;

static void set_ignore(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

struct statement *ignored_stmt;
static void trigger_list_del(struct sm_state *sm, struct expression *mod_expr)
{
	char buf[64];
	char *p;

	if (__cur_stmt == ignored_stmt)
		return;

	snprintf(buf, sizeof(buf), "&%s", sm->name);
	p = strstr(buf, ".next");
	if (!p)
		return;
	*p = '\0';
	set_state(my_id, buf, sm->sym, &undefined);
}

static void set_up_next_trigger(struct expression *expr)
{
	struct symbol *type, *member;
	struct expression *next;

	type = get_pointer_type(expr);
	if (!type || type->type != SYM_STRUCT)
		return;
	member = first_ptr_list((struct ptr_list *)type->symbol_list);
	if (!member || !member->ident ||
	    strcmp(member->ident->name, "next") != 0)
		return;

	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = expr->unop;

	next = member_expression(expr, '.', member->ident);
	set_state_expr(next_id, next, &trigger);
	ignored_stmt = __cur_stmt;
}

static void match_add(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, 0);
	set_state_expr(my_id, arg, &added);
	set_up_next_trigger(arg);
}

static void match_del(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, 0);
	if (!get_state_expr(my_id, arg)) {
		silenced = true;
		return;
	}
	set_state_expr(my_id, arg, &undefined);
}

static void match_free(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;
	struct sm_state *sm;
	char *name;
	struct symbol *sym;

	if (silenced)
		return;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(param));
	if (!arg)
		return;
	name = expr_to_var_sym(arg, &sym);
	if (!name || !sym)
		goto free;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state == &undefined)
			continue;
		if (sm->sym != sym)
			continue;
		if (strncmp(sm->name + 1, name, strlen(name)) != 0)
			continue;
		sm_warning("'%s' not removed from list", sm->name);
		goto free;
	} END_FOR_EACH_SM(sm);
free:
	free_string(name);
}

void check_list_add(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&silenced);

	add_function_hook("list_add", &match_add, NULL);
	add_function_hook("list_add_tail", &match_add, NULL);
	add_function_hook("list_del", &match_del, NULL);

	add_modification_hook(my_id, &set_ignore);

	add_function_hook("kfree", &match_free, 0);
}

void check_list_add_late(int id)
{
	next_id = id;
	add_modification_hook(next_id, &trigger_list_del);
}
