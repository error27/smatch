/*
 * sparse/check_assigned_expr.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * This is not a check.  It just saves an struct expression pointer 
 * whenever something is assigned.  This can be used later on by other scripts.
 */

#include "smatch.h"
#include "smatch_slist.h"

int check_assigned_expr_id;
static int my_id;

struct expression *get_assigned_expr(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_id, expr);
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
	name = expr_to_str_complex(expr);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

static void match_assignment(struct expression *expr)
{
	if (expr->op != '=')
		return;
	set_state_expr(my_id, expr->left, alloc_my_state(expr->right));
}

void check_assigned_expr(int id)
{
	my_id = check_assigned_expr_id = id;
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
}
