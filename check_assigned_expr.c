/*
 * Copyright (C) 2009 Dan Carpenter.
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
 * This is not a check.  It just saves an struct expression pointer 
 * whenever something is assigned.  This can be used later on by other scripts.
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

int check_assigned_expr_id;
static int my_id;
static int link_id;

static void undef(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

struct expression *get_assigned_expr(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
	if (!state)
		return NULL;
	return (struct expression *)state->data;
}

struct expression *get_assigned_expr_name_sym(const char *name, struct symbol *sym)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (!state)
		return NULL;
	return (struct expression *)state->data;
}

static struct smatch_state *alloc_my_state(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	state = __alloc_smatch_state(0);
	expr = strip_expr(expr);
	name = expr_to_str(expr);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

static void match_assignment(struct expression *expr)
{
	struct symbol *left_sym, *right_sym;
	char *left_name = NULL;
	char *right_name = NULL;

	if (expr->op != '=')
		return;
	if (is_fake_call(expr->right))
		return;

	left_name = expr_to_var_sym(expr->left, &left_sym);
	if (!left_name || !left_sym)
		goto free;
	set_state(my_id, left_name, left_sym, alloc_my_state(expr->right));

	right_name = expr_to_var_sym(expr->right, &right_sym);
	if (!right_name || !right_sym)
		goto free;

	store_link(link_id, right_name, right_sym, left_name, left_sym);

free:
	free_string(left_name);
	free_string(right_name);
}

void check_assigned_expr(int id)
{
	my_id = check_assigned_expr_id = id;
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
	add_modification_hook(my_id, &undef);
}

void check_assigned_expr_links(int id)
{
	link_id = id;
	set_up_link_functions(my_id, link_id);
}

