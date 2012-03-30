/*
 * sparse/check_get_user_overflow.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Looks for integers that we get from the user which can be attacked
 * with an integer overflow.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_max_id;
static int my_min_id;

STATE(capped);
STATE(user_data);

static void match_condition(struct expression *expr)
{
	struct smatch_state *left_max_true = NULL;
	struct smatch_state *left_max_false = NULL;
	struct smatch_state *right_max_true = NULL;
	struct smatch_state *right_max_false = NULL;

	struct smatch_state *left_min_true = NULL;
	struct smatch_state *left_min_false = NULL;
	struct smatch_state *right_min_true = NULL;
	struct smatch_state *right_min_false = NULL;


	switch (expr->op) {
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		left_max_true = &capped;
		right_max_false = &capped;
		right_min_true = &capped;
		left_min_false = &capped;
		break;
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		left_max_false = &capped;
		right_max_true = &capped;
		left_min_true = &capped;
		right_min_false = &capped;
		break;
	case SPECIAL_EQUAL:
		left_max_true = &capped;
		right_max_true = &capped;
		left_min_true = &capped;
		right_min_true = &capped;
		break;
	case SPECIAL_NOTEQUAL:
		left_max_false = &capped;
		right_max_false = &capped;
		left_min_false = &capped;
		right_min_false = &capped;
		break;
	default:
		return;
	}

	if (get_state_expr(my_max_id, expr->left)) {
		set_true_false_states_expr(my_max_id, expr->left, left_max_true, left_max_false);
		set_true_false_states_expr(my_min_id, expr->left, left_min_true, left_min_false);
	}
	if (get_state_expr(my_max_id, expr->right)) {
		set_true_false_states_expr(my_max_id, expr->right, right_max_true, right_max_false);
		set_true_false_states_expr(my_min_id, expr->right, right_min_true, right_min_false);
	}
}

static void match_normal_assign(struct expression *expr)
{
	if (get_state_expr(my_max_id, expr->left)) {
		set_state_expr(my_max_id, expr->left, &capped);
		set_state_expr(my_min_id, expr->left, &capped);
	}
}

static void match_assign(struct expression *expr)
{
	char *name;

	name = get_macro_name(expr->pos);
	if (!name || strcmp(name, "get_user") != 0) {
		match_normal_assign(expr);
		return;
	}
	name = get_variable_from_expr(expr->right, NULL);
	if (!name || strcmp(name, "__val_gu") != 0)
		goto free;
	set_state_expr(my_max_id, expr->left, &user_data);
	set_state_expr(my_min_id, expr->left, &user_data);
free:
	free_string(name);
}

static void check_expr(struct expression *expr)
{
	struct sm_state *sm;
	long long val;
	char *name;
	int overflow = 0;
	int underflow = 0;

	sm = get_sm_state_expr(my_max_id, expr);
	if (sm && slist_has_state(sm->possible, &user_data)) {
		if (!get_absolute_max(expr, &val) || val > 20000)
			overflow = 1;
	}

	sm = get_sm_state_expr(my_min_id, expr);
	if (sm && slist_has_state(sm->possible, &user_data)) {
		if (!get_absolute_min(expr, &val) || val < -20000)
			underflow = 1;
	}

	if (!overflow && !underflow)
		return;

	name = get_variable_from_expr(expr, NULL);
	if (overflow && underflow) 
		sm_msg("warn: check for integer over/underflow '%s'", name);
	else if (underflow)
		sm_msg("warn: check for integer underflow '%s'", name);
	else
		sm_msg("warn: check for integer overflow '%s'", name);
	free_string(name);

	set_state_expr(my_max_id, expr, &capped);
	set_state_expr(my_min_id, expr, &capped);
}

static void match_binop(struct expression *expr)
{
	if (expr->op == '^')
		return;
	if (expr->op == '&')
		return;
	if (expr->op == '|')
		return;
	if (expr->op == SPECIAL_RIGHTSHIFT)
		return;
	if (expr->op == SPECIAL_LEFTSHIFT)
		return;

	check_expr(expr->left);
	check_expr(expr->right);
}

void check_get_user_overflow(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_max_id = id;
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_binop, BINOP_HOOK);
}

void check_get_user_overflow2(int id)
{
	my_min_id = id;
}
