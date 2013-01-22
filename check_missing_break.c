/*
 * sparse/check_missing_break.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The way I'm detecting missing breaks is if there is an assignment inside a
 * switch statement which is over written.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;
static struct expression *skip_this;

/*
 * It goes like this:
 * - Allocate a state which stores the switch expression.  I wanted to
 *   just have a state &assigned but we need to know the switch statement where
 *   it was assigned.
 * - If it gets used then we change it to &used.
 * - For unmatched states we use &used (because of cleanness, not because we need
 *   to).
 * - If we merge inside a case statement and one of the states is &assigned (or
 *   if it is &nobreak) then &nobreak is used.
 *
 * We print an error when we assign something to a &no_break symbol.
 *
 */

STATE(used);
STATE(no_break);

static struct smatch_state *alloc_my_state(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	state = __alloc_smatch_state(0);
	expr = strip_expr(expr);
	name = expr_to_str_complex(expr);
	if (!name)
		name = alloc_string("");
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

struct expression *last_print_expr;
static void print_missing_break(struct expression *expr)
{
	char *name;

	if (get_switch_expr() == last_print_expr)
		return;
	last_print_expr = get_switch_expr();

	name = expr_to_str(expr);
	sm_msg("warn: missing break? reassigning '%s'", name);
	free_string(name);
}

static void match_assign(struct expression *expr)
{
	struct expression *left;

	if (expr->op != '=')
		return;
	if (!get_switch_expr())
		return;
	left = strip_expr(expr->left);
	if (get_state_expr(my_id, left) == &no_break)
		print_missing_break(left);

	set_state_expr(my_id, left, alloc_my_state(get_switch_expr()));
	skip_this = left;
}

static void match_symbol(struct expression *expr)
{
	expr = strip_expr(expr);
	if (expr == skip_this)
		return;
	set_state_expr(my_id, expr, &used);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &used;
}

static int in_case;
struct smatch_state *merge_hook(struct smatch_state *s1, struct smatch_state *s2)
{
	struct expression *switch_expr;

	if (s1 == &no_break || s2 == &no_break)
		return &no_break;
	if (!in_case)
		return &used;
	switch_expr = get_switch_expr();
	if (s1->data == switch_expr || s2->data == switch_expr)
		return &no_break;
	return &used;
}

static void match_stmt(struct statement *stmt)
{
	if (stmt->type == STMT_CASE)
		in_case = 1;
	else
		in_case = 0;
}

void check_missing_break(int id)
{
	my_id = id;

	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_hook);

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_symbol, SYM_HOOK);
	add_hook(&match_stmt, STMT_HOOK);
}
