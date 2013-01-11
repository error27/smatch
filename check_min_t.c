/*
 * smatch/check_min_t.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_assign(struct expression *expr)
{
	const char *macro;
	sval_t max_left, max_right;
	char *name;

	if (expr->op != '=')
		return;

	macro = get_macro_name(expr->pos);
	if (!macro)
		return;
	if (strcmp(macro, "min_t"))
		return;

	if (!get_absolute_max(expr->left, &max_left))
		return;
	if (!get_absolute_max(expr->right, &max_right))
		return;

	if (sval_cmp(max_left, max_right) >= 0)
		return;

	name = expr_to_str_complex(expr->right);
	sm_msg("warn: min_t truncates here '%s' (%s vs %s)", name, sval_to_str(max_left), sval_to_str(max_right));
	free_string(name);
}

void check_min_t(int id)
{
	my_id = id;
	if (option_project != PROJ_KERNEL)
		return;
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
