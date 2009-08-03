/*
 * sparse/smatch_containers.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static struct tracker_list *member_list;

int is_member(struct expression *expr)
{
	if (expr->type == EXPR_DEREF)
		return 1;
	return 0;
}

void set_default_state(int owner, struct smatch_state *state)
{
	default_state[owner] = state;
}

void reset_on_container_modified(int owner, struct expression *expr) 
{
	char *name;
	struct symbol *sym;

	return;  /* this stuff is taking too long */

	if (!is_member(expr))
		return;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name  || !sym)
		goto free;
	add_tracker(&member_list, name, owner, sym);
free:
	free_string(name);
}

static void match_assign_call(struct expression *expr)
{
	struct tracker *tmp;
	struct symbol *sym;
	char *name;
	struct expression *left;

	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name || !sym)
		goto free;

	FOR_EACH_PTR(member_list, tmp) {
		if (!default_state[tmp->owner])
			continue;
		if (tmp->sym == sym 
			&& !strncmp(tmp->name, name, strlen(name))) {
			set_state(tmp->name, tmp->owner, tmp->sym, default_state[tmp->owner]);
			goto free;
		}
	} END_FOR_EACH_PTR(tmp);
free:
	free_string(name);
}

static void match_func_end(struct symbol *sym)
{
	free_trackers_and_list(&member_list);
}

void register_containers(int id)
{
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_func_end, END_FUNC_HOOK);
}
