/*
 * smatch/smatch_capped.c
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * This is trying to make a list of the variables which
 * have capped values.  Sometimes we don't know what the
 * cap is, for example if we are comparing variables but
 * we don't know the values of the variables.  In that
 * case we only know that our variable is capped and we
 * sort that information here.
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(capped);
STATE(uncapped);

static int is_capped_macro(struct expression *expr)
{
	char *name;

	name = get_macro_name(expr->pos);
	if (!name)
		return 0;

	if (strcmp(name, "min") == 0)
		return 1;
	if (strcmp(name, "MIN") == 0)
		return 1;
	if (strcmp(name, "min_t") == 0)
		return 1;

	return 0;
}

int is_capped(struct expression *expr)
{
	sval_t sval;

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	if (is_capped_macro(expr))
		return 1;

	if (expr->type == EXPR_BINOP) {
		if (expr->op == '&')
			return 1;
		if (expr->op == SPECIAL_RIGHTSHIFT)
			return 1;
		if (expr->op == '%')
			return is_capped(expr->right);
		if (!is_capped(expr->left))
			return 0;
		if (expr->op == '/')
			return 1;
		if (!is_capped(expr->right))
			return 0;
		return 1;
	}
	if (get_implied_max(expr, &sval))
		return 1;
	if (get_state_expr(my_id, expr) == &capped)
		return 1;
	return 0;
}

void set_param_capped_data(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];

	if (strncmp(key, "$$", 2))
		return;
	snprintf(fullname, 256, "%s%s", name, key + 2);
	set_state(my_id, fullname, sym, &capped);
}

static void match_condition(struct expression *expr)
{
	struct smatch_state *left_true = NULL;
	struct smatch_state *left_false = NULL;
	struct smatch_state *right_true = NULL;
	struct smatch_state *right_false = NULL;


	if (expr->type != EXPR_COMPARE)
		return;

	switch (expr->op) {
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		left_true = &capped;
		right_false = &capped;
		break;
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		left_false = &capped;
		right_true = &capped;
		break;
	case SPECIAL_EQUAL:
		left_true = &capped;
		right_true = &capped;
		break;
	case SPECIAL_NOTEQUAL:
		left_false = &capped;
		right_false = &capped;
		break;

	default:
		return;
	}

	set_true_false_states_expr(my_id, expr->right, right_true, right_false);
	set_true_false_states_expr(my_id, expr->left, left_true, left_false);
}

static void match_assign(struct expression *expr)
{
	if (is_capped(expr->right)) {
		set_state_expr(my_id, expr->left, &capped);
	} else {
		if (get_state_expr(my_id, expr->left))
			set_state_expr(my_id, expr->left, &uncapped);
	}
}

static void match_caller_info(struct expression *expr)
{
	struct expression *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		if (is_capped(tmp))
			sql_insert_caller_info(expr, CAPPED_DATA, i, "$$", "1");
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct smatch_state *state)
{
	if (state != &capped)
		return;
	sql_insert_caller_info(call, CAPPED_DATA, param, printed_name, "1");
}

void register_capped(int id)
{
	my_id = id;

	add_definition_db_callback(set_param_capped_data, CAPPED_DATA);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	if (option_info) {
		add_hook(&match_caller_info, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_id, struct_member_callback);
	}
}
