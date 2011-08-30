/*
 * sparse/check_param_values.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * 
 *
 */

#include "smatch.h"

static int my_id;

STATE(user_data);

static void match_assign(struct expression *expr)
{
	char *name;

	name = get_macro_name(&expr->pos);
	if (!name || strcmp(name, "get_user") != 0)
		return;
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

static void match_call(struct expression *expr)
{
	struct expression *tmp;
	char *func;
	int i;

	if (expr->fn->type != EXPR_SYMBOL)
		return;

	func = expr->fn->symbol_name->name;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		if (get_state_expr(my_id, tmp) != &user_data) {
			i++;
			continue;
		}
		sm_msg("info: user_data %s %d", func, i);
		i++;
	} END_FOR_EACH_PTR(tmp);
}

void check_param_values(int id)
{
	if (!option_info)
		return;
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_function_hook("copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("__copy_from_user", &match_user_copy, INT_PTR(0));
	add_function_hook("memcpy_fromiovec", &match_user_copy, INT_PTR(0));
	// memdup_user()
}
