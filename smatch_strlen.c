/*
 * smatch/smatch_strlen.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_strlen_id;

static void set_strlen_undefined(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(sm->owner, sm->name, sm->sym, &undefined);
}

static void match_strlen(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *str;
	struct expression *len_expr;
	char *len_name;
	struct smatch_state *state;

	right = strip_expr(expr->right);
	str = get_argument_from_call_expr(right->args, 0);
	len_expr = strip_expr(expr->left);

	len_name = expr_to_var(len_expr);
	if (!len_name)
		return;

	state = __alloc_smatch_state(0);
        state->name = len_name;
	state->data = len_expr;

	set_state_expr(my_strlen_id, str, state);
}

int get_implied_strlen(struct expression *expr, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state_expr(my_strlen_id, expr);
	if (!state || !state->data)
		return 0;
	if (!get_implied_rl((struct expression *)state->data, rl))
		return 0;
	return 1;
}

int get_size_from_strlen(struct expression *expr)
{
	struct range_list *rl;
	sval_t max;

	if (!get_implied_strlen(expr, &rl))
		return 0;
	max = rl_max(rl);
	if (sval_is_negative(max) || sval_is_max(max))
		return 0;

	return max.value + 1; /* add one because strlen doesn't include the NULL */
}

void register_strlen(int id)
{
	my_strlen_id = id;
	add_function_assign_hook("strlen", &match_strlen, NULL);
	add_modification_hook(my_strlen_id, &set_strlen_undefined);
}

