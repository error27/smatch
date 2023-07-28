/*
 * Copyright (C) 2020 Oracle.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(iterator);

static struct expression *get_iterator(struct statement *stmt)
{
	struct expression *expr;

	if (!stmt ||
	    stmt->type != STMT_ITERATOR ||
	    !stmt->iterator_pre_statement ||
	    stmt->iterator_pre_statement->type != STMT_EXPRESSION)
		return NULL;

	expr = strip_expr(stmt->iterator_pre_statement->expression);
	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return NULL;

	return strip_expr(expr->left);
}

static void match_loop(struct statement *stmt)
{
	struct expression *pos;
	char *macro;

	if (stmt->type != STMT_ITERATOR)
		return;
	if (!stmt->iterator_pre_statement ||
	    !stmt->iterator_pre_condition ||
	    !stmt->iterator_post_statement)
		return;

	macro = get_macro_name(stmt->pos);
	if (!macro)
		return;
	if (strncmp(macro, "list_for_each", strlen("list_for_each")) != 0)
		return;
	pos = get_iterator(stmt);
	if (!pos)
		return;

	set_state_expr(my_id, pos, &iterator);
}

static bool getting_prev_next(struct expression *expr)
{
	struct expression *parent;
	int cnt = 0;

	parent = expr;
	while ((parent = expr_get_parent_expr(parent))) {
		if (parent->type == EXPR_PREOP && parent->op == '(')
			continue;
		if (parent->type == EXPR_DEREF &&
		    parent->member &&
		    (strcmp(parent->member->name, "prev") == 0 ||
		     strcmp(parent->member->name, "next") == 0))
			return true;
		if (cnt++ > 3)
			break;
	}

	return false;
}

static void match_dereference(struct expression *expr)
{
	struct expression *orig = expr;
	struct sm_state *sm;
	char *name;

	if (expr->type == EXPR_PREOP && expr->op == '*')
		expr = strip_expr(expr->unop);

	sm = get_sm_state_expr(my_id, expr);
	if (!sm || !slist_has_state(sm->possible, &iterator))
		return;

	if (getting_prev_next(orig))
		return;

	name = expr_to_str(expr);
	sm_warning("iterator used outside loop: '%s'", name);
	free_string(name);

	set_state_expr(my_id, expr, &undefined);
}

void check_iterator_outside_loop(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_hook(match_loop, AFTER_LOOP_NO_BREAKS);
	add_modification_hook(my_id, &set_undefined);
	add_hook(&match_dereference, DEREF_HOOK);
}
