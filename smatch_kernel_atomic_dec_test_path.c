/*
 * Copyright (C) 2016 Oracle.
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

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(zero_path);

static void handle_test_functions(struct expression *expr)
{
	struct expression *tmp;
	struct statement *stmt;
	int count = 0;

	while (expr && expr->type == EXPR_ASSIGNMENT && expr->op == '=')
		expr = strip_expr(expr->right);

	if (!expr ||
	    expr->type != EXPR_CALL ||
	    expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol_name)
		return;
	if (!strstr(expr->fn->symbol_name->name, "test"))
		return;

	while ((tmp = expr_get_parent_expr(expr))) {
		expr = tmp;
		if (count++ > 5)
			break;
	}

	/*
	 * FIXME: This doesn't work for assignments like:
	 *
	 * free = dec_and_test(foo);
	 * if (free)
	 *	free_thing(x);
	 *
	 * Maybe we should just say everything is on an atomic_dec path at this
	 * point.  That probably works fine.  The other solution would be to
	 * store the variable and look up the state of it or something...
	 *
	 */

	stmt = expr_get_parent_stmt(expr);
	if (!stmt || stmt->type != STMT_IF) {
		/* See above comment.  I decided to go with the easy option */
		set_state(my_id, "dec_path", NULL, &zero_path);
		return;
	}

	set_true_false_states(my_id, "dec_path", NULL, &zero_path, NULL);
}

static void match_dec(struct expression *expr, const char *name, struct symbol *sym)
{
	handle_test_functions(expr);
}

int on_atomic_dec_path(void)
{
	return get_state(my_id, "dec_path", NULL) == &zero_path;
}

void register_kernel_atomic_dec_test_path(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_refcount_dec_hook(&match_dec);
}
