/*
 * Copyright (C) 2014 Oracle.
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

#define _GNU_SOURCE
#include <string.h>
#include "smatch.h"
#include "smatch_extra.h"

static int my_id;

static bool in_same_block(struct expression *one, struct expression *two)
{
	struct statement *a, *b;

	a = get_parent_stmt(one);
	b = get_parent_stmt(two);
	if (!a || !b)
		return false;
	return a->parent == b->parent;
}

static bool is_loop_condition(struct expression *expr)
{
	struct statement *stmt;

	/*
	 * Two things.  First of all get_stored_condition() is buggy.
	 * Secondly, even if it were not buggy there would be an
	 * issue checking the pre condition before the loop runs.
	 */
	stmt = get_parent_stmt(expr);
	if (stmt && stmt->type == STMT_ITERATOR)
		return true;
	return false;
}

static bool is_part_of_logical(struct expression *expr)
{
	while ((expr = expr_get_parent_expr(expr))) {
		if (expr->type == EXPR_PREOP) {
		    if (expr->op == '!' ||
			expr->op == '(')
			continue;
		}
		if (expr->type == EXPR_COMPARE)
			continue;
		if (expr->type == EXPR_LOGICAL)
			return true;
		return false;
	}
	return false;
}

static bool last_in_chain_of_else_if_statements(struct expression *expr)
{
	struct statement *stmt;

	stmt = get_parent_stmt(expr);
	if (!stmt)
		return false;
	if (stmt->type != STMT_IF)
		return false;
	if (stmt->if_false)
		return false;
	stmt = stmt_get_parent_stmt(stmt);
	if (!stmt)
		return false;
	if (stmt->type != STMT_IF)
		return false;
	return true;
}

static bool is_global(struct expression *expr)
{
	struct symbol *sym;

	sym = expr_to_sym(expr);
	if (!sym)
		return false;
	return !!(sym->ctype.modifiers & MOD_TOPLEVEL);
}

static bool is_dereference(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_COMPARE ||
	    expr->type == EXPR_BINOP) {
		if (is_dereference(expr->left) ||
		    is_dereference(expr->right))
			return true;
		return false;
	}
	if (expr->type != EXPR_DEREF)
		return false;
	return true;
}

static void match_condition(struct expression *expr)
{
	struct expression *old_condition;
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	if (__in_fake_parameter_assign)
		return;

	if (get_macro_name(expr->pos))
		return;

	if (is_loop_condition(expr))
		return;

	if (is_part_of_logical(expr))
		return;

	if (last_in_chain_of_else_if_statements(expr))
		return;

	if (is_global(expr))
		return;

	if (is_dereference(expr))
		return;

	state = get_stored_condition(expr);
	if (!state || !state->data)
		return;
	old_condition = state->data;
	if (get_macro_name(old_condition->pos))
		return;

	if (inside_loop() && !in_same_block(old_condition, expr))
		return;

	name = expr_to_str_sym(expr, &sym);
	state = get_state(my_id, name, sym);
	if (state != &true_state && state != &false_state)
		goto free;

	sm_warning("duplicate check '%s' (previous on line %d)", name, old_condition->pos.line);
free:
	free_string(name);
}

static bool has_array(struct expression *expr)
{
	if (!expr)
		return false;

	if (is_array(expr))
		return true;
	if (expr->type == EXPR_COMPARE)
		return is_array(expr->left);
	return false;
}

static void match_condition_store(struct expression *expr)
{
	struct symbol *sym;
	sval_t dummy;
	char *name;

	if (get_value(expr, &dummy))
		return;

	if (has_array(expr))
		return;

	name = expr_to_str_sym(expr, &sym);
	if (!name)
		return;

	if (sym && sym->ctype.modifiers & MOD_TOPLEVEL)
		goto free;

	set_true_false_states(my_id, name, sym, &true_state, &false_state);
free:
	free_string(name);
}

void check_double_checking(int id)
{
	my_id = id;

	if (!option_spammy)
		return;

	turn_off_implications(my_id);

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_condition_store, CONDITION_HOOK);
}
