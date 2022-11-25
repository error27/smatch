/*
 * Copyright (C) 2022 Dan Carpenter.
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

static struct statement *get_if_statement(struct expression *expr)
{
	struct statement *stmt;

	stmt = get_parent_stmt(expr);
	if (!stmt || stmt->type != STMT_RETURN)
		return NULL;
	stmt = stmt_get_parent_stmt(stmt);
	if (stmt && stmt->type == STMT_COMPOUND)
		stmt = stmt_get_parent_stmt(stmt);
	if (!stmt)
		return NULL;

	if (stmt->type != STMT_IF)
		return NULL;

	return stmt;
}

static bool condition_matches(struct expression *cond, struct expression *expr)
{
	expr = strip_expr(expr);
	cond = strip_expr(cond);

	while (cond) {
		if (cond->type == EXPR_PREOP &&
		    (cond->op == '(' || cond->op == '!')) {
			cond = strip_expr(cond->unop);
			continue;
		}
		if (cond->type == EXPR_COMPARE &&
		    (cond->op == SPECIAL_EQUAL || cond->op == SPECIAL_NOTEQUAL) &&
		     expr_is_zero(cond->right)) {
			cond = strip_expr(cond->left);
			continue;
		}
		break;
	}

	if (expr_equiv(cond, expr))
		return true;

	return false;
}

static struct expression *get_orig_call(struct expression *expr)
{
	struct expression *orig;

	orig = get_assigned_expr(expr);
	if (!orig || orig->type != EXPR_CALL)
		return NULL;

	return orig;
}

static void match_return(struct expression *expr)
{
	struct expression *call;
	struct statement *stmt;
	sval_t sval;
	char *name;

	if (!expr || expr->type != EXPR_SYMBOL)
		return;

	if (get_type(expr) != &int_ctype)
		return;

	if (!get_implied_value(expr, &sval) || sval.value != 0)
		return;

	stmt = get_if_statement(expr);
	if (!stmt)
		return;
	if (condition_matches(stmt->if_conditional, expr))
		return;

	call = get_orig_call(expr);
	if (!call)
		return;

	if (call->pos.line >= stmt->pos.line)
		return;

	if (call->pos.line + 5 >= expr->pos.line)
		return;

	name = expr_to_str(expr);
	sm_warning("missing error code? '%s'", name);
	free_string(name);
}

void check_missing_error_code2(int id)
{
	my_id = id;

	add_hook(&match_return, RETURN_HOOK);
}
