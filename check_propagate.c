/*
 * Copyright (C) 2010 Dan Carpenter.
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
 * People should just return the error codes they received
 * instead of making up there own dumb error codes all the time.
 */

#include "smatch.h"

static int my_id;

static struct expression *last_return;
static struct expression *last_func;

static char *get_fn_name(struct expression *expr)
{
	if (expr->type != EXPR_CALL)
		return NULL;
	if (expr->fn->type != EXPR_SYMBOL)
		return NULL;
	return expr_to_var(expr->fn);
}

static void match_call_assignment(struct expression *expr)
{
	if (get_macro_name(expr->left->pos))
		return;
	last_return = expr->left;
	last_func = expr->right;
}

static void match_unset(struct expression *expr)
{
	last_return = NULL;
}

static void match_return(struct expression *ret_value)
{
	sval_t rval;
	sval_t lret;
	char *name;

	if (!get_value(ret_value, &rval) || rval.value >= 0)
		return;
	if (get_implied_value(last_return, &lret))
		return;
	if (!get_implied_max(last_return, &lret) || lret.value >= 0)
		return;
	if (get_implied_min(last_return, &lret) && !sval_is_min(lret))
		return;
	name = expr_to_var(last_return);
	sm_msg("info: why not propagate '%s' from %s() instead of %s?",
		name, get_fn_name(last_func), sval_to_str(rval));
	free_string(name);
}

void check_propagate(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_unset, ASSIGNMENT_HOOK);
	add_hook(&match_call_assignment, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_return, RETURN_HOOK);
}
