/*
 * Copyright (C) 2012 Oracle.
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
 * This is an --info recipe.  The goal is to print a message for every parameter
 * which we can not avoid dereferencing.  This is maybe a bit restrictive but it
 * avoids some false positives.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(derefed);
STATE(ignore);
STATE(param);

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	if (cur->state == &derefed || other->state != &derefed)
		return;
	if (is_impossible_path())
		set_state(my_id, cur->name, cur->sym, &derefed);
}

static void set_ignore(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state == &derefed)
		return;
	set_state(my_id, sm->name, sm->sym, &ignore);
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;
	int i;

	i = -1;
	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		i++;
		if (!arg->ident)
			continue;
		set_state(my_id, arg->ident->name, arg, &param);
	} END_FOR_EACH_PTR(arg);
}

static int is_ignored_param(struct expression *expr)
{
	struct sm_state *sm;

	if (param_was_set(expr))
		return 1;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &ignore))
		return 1;
	return 0;
}

static void check_deref(struct expression *expr)
{
	struct expression *tmp;

	if (is_impossible_path())
		return;

	tmp = get_assigned_expr(expr);
	if (tmp)
		expr = tmp;

	if (expr->type == EXPR_PREOP &&
	    expr->op == '&') {
		expr = strip_expr(expr->unop);
		if (expr->type != EXPR_DEREF)
			return;
		expr = strip_expr(expr->deref);
		if (expr->type != EXPR_PREOP ||
		    expr->op != '*')
			return;
		expr = strip_expr(expr->unop);
	}

	expr = strip_expr(expr);
	if (!expr)
		return;

	if (expr->type == EXPR_PREOP && expr->op == '&')
		return;

	if (get_param_num(expr) < 0)
		return;

	if (is_ignored_param(expr))
		return;

	if (param_was_set(expr))
		return;

	/*
	 * At this point we really only care about potential NULL dereferences.
	 * Potentially in the future we will care about everything.
	 */
	if (implied_not_equal(expr, 0))
		return;

	set_state_expr(my_id, expr, &derefed);
}

static void match_dereference(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	check_deref(expr->unop);
}

static void find_inner_dereferences(struct expression *expr)
{
	while (expr->type == EXPR_PREOP) {
		if (expr->op == '*')
			check_deref(expr->unop);
		expr = strip_expr(expr->unop);
	}
}

static void set_param_dereferenced(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;

	check_deref(arg);

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;
	if (is_ignored_param(symbol_expression(sym)))
		goto free;
	if (get_param_num_from_sym(sym) < 0)
		goto free;
	if (param_was_set_var_sym(name, sym))
		goto free;

	set_state(my_id, name, sym, &derefed);
	find_inner_dereferences(arg);
free:
	free_string(name);
}

static void process_states(void)
{
	struct sm_state *tmp;
	int arg;
	const char *name;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), tmp) {
		if (tmp->state != &derefed)
			continue;
		arg = get_param_num_from_sym(tmp->sym);
		if (arg < 0)
			continue;
		name = get_param_name(tmp);
		if (!name || name[0] == '&')
			continue;
		sql_insert_return_implies(DEREFERENCE, arg, name, "1");
	} END_FOR_EACH_SM(tmp);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	check_deref(get_array_base(expr));
}

void check_dereferences_param(int id)
{
	my_id = id;

	add_hook(&match_function_def, FUNC_DEF_HOOK);

	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_return_implies_hook(DEREFERENCE, &set_param_dereferenced);
	add_modification_hook(my_id, &set_ignore);
	add_pre_merge_hook(my_id, &pre_merge_hook);

	all_return_states_hook(&process_states);
}
