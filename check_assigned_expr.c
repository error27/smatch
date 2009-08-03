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

static struct smatch_state *alloc_my_state(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	state = malloc(sizeof(*state));
	expr = strip_expr(expr);
	name = get_variable_from_expr_complex(expr, NULL);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

static void match_assignment(struct expression *expr)
{
	set_state_expr(my_id, expr->left, alloc_my_state(expr->right));
}

void check_assigned_expr(int id)
{
	my_id = check_assigned_expr_id = id;
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
}
