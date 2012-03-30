/*
 * smatch/check_user_data.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * There are a couple checks that try to see if a variable
 * comes from the user.  It would be better to unify them
 * into one place.  Also it we should follow the data down
 * the call paths.  Hence this file.
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(capped);
STATE(user_data);

int is_user_data(struct expression *expr)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp;
	struct symbol *sym;
	char *name;
	int user = 0;

	expr = strip_expr(expr);
	if (is_capped(expr))
		return 0;
	if (expr->type == EXPR_BINOP) {
		if (is_user_data(expr->left))
			return 1;
		if (is_user_data(expr->right))
			return 1;
		return 0;
	}
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);

	tmp = get_sm_state_expr(my_id, expr);
	if (tmp)
		return slist_has_state(tmp->possible, &user_data);

	name = get_variable_from_expr_complex(expr, &sym);
	if (!name || !sym)
		goto free;

	slist = get_all_states(my_id);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->sym != sym)
			continue;
		if (!strncmp(tmp->name, name, strlen(tmp->name))) {
			if (slist_has_state(tmp->possible, &user_data))
				user = 1;
			goto free;
		}
	} END_FOR_EACH_PTR(tmp);

free:
	free_slist(&slist);
	free_string(name);
	return user;
}

void set_param_user_data(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];

	if (strncmp(key, "$$", 2))
		return;
	snprintf(fullname, 256, "%s%s", name, key + 2);
	set_state(my_id, fullname, sym, &user_data);
}

static void match_condition(struct expression *expr)
{
	switch (expr->op) {
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, &capped, NULL);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, NULL, &capped);
		break;
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, &capped, NULL);
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, NULL, &capped);
		break;
	case SPECIAL_EQUAL:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, &capped, NULL);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, &capped, NULL);
		break;
	case SPECIAL_NOTEQUAL:
		if (is_user_data(expr->left))
			set_true_false_states_expr(my_id, expr->left, NULL, &capped);
		if (is_user_data(expr->right))
			set_true_false_states_expr(my_id, expr->right, NULL, &capped);
		break;
	default:
		return;
	}

}

static void match_normal_assign(struct expression *expr)
{
	if (is_user_data(expr->left))
		set_state_expr(my_id, expr->left, &capped);
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
	set_state_expr(my_id, expr->left, &user_data);
free:
	free_string(name);
}

static void match_user_copy(const char *fn, struct expression *expr, void *_param)
{
	int param = PTR_INT(_param);
	struct expression *dest;

	dest = get_argument_from_call_expr(expr->args, param);
	dest = strip_expr(dest);
	if (!dest)
		return;
	/* the first thing I tested this on pass &foo to a function */
	set_state_expr(my_id, dest, &user_data);
	if (dest->type == EXPR_PREOP && dest->op == '&') {
		/* but normally I'd think it would pass the actual variable */
		dest = dest->unop;
		set_state_expr(my_id, dest, &user_data);
	}
}

static void match_user_assign_function(const char *fn, struct expression *expr, void *unused)
{
	set_state_expr(my_id, expr->left, &user_data);
}

static void match_assign_userdata(struct expression *expr)
{
	if (is_user_data(expr->right))
		set_state_expr(my_id, expr->left, &user_data);
}

static void match_caller_info(struct expression *expr)
{
	struct expression *tmp;
	char *func;
	int i;

	func = get_fnptr_name(expr->fn);
	if (!func)
		return;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		if (is_user_data(tmp))
			sm_msg("info: passes user_data %s %d '$$'", func, i);
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void struct_member_callback(char *fn, int param, char *printed_name, struct smatch_state *state)
{
	if (state == &capped)
		return;
	sm_msg("info: passes user_data '%s' %d '%s'", fn, param, printed_name);
}

void check_user_data(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	add_definition_db_callback(set_param_user_data, USER_DATA);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_assign_userdata, ASSIGNMENT_HOOK);
	add_function_hook("copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("__copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
	add_function_assign_hook("kmemdup_user", &match_user_assign_function, NULL);
	if (option_info) {
		add_hook(&match_caller_info, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_id, struct_member_callback);
	}
}
