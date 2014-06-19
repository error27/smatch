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

/*
 * This file is sort of like check_dereferences_param.c.  In theory the one
 * difference should be that the param is NULL it should still be counted as a
 * free.  But for now I don't handle that case.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(freed);
STATE(ignore);
STATE(param);

static int is_arg(struct expression *expr)
{
	struct symbol *arg;

	if (expr->type != EXPR_SYMBOL)
		return 0;

	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		if (arg == expr->symbol)
			return 1;
	} END_FOR_EACH_PTR(arg);
	return 0;
}

static void set_ignore(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state == &freed)
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

static void freed_variable(struct expression *expr)
{
	struct sm_state *sm;

	expr = strip_expr(expr);
	if (!is_arg(expr))
		return;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &ignore))
		return;
	set_state_expr(my_id, expr, &freed);
}

static void match_free(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(param));
	if (!arg)
		return;
	freed_variable(arg);
}

static void set_param_freed(struct expression *arg, char *key, char *unused)
{
	/* XXX FIXME: call_implies has been updated with more information */
	if (strcmp(key, "$$") != 0)
		return;
	freed_variable(arg);
}

static void process_states(struct stree *stree)
{
	struct symbol *arg;
	int i;

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		i++;
		if (!arg->ident)
			continue;
		if (get_state_stree(stree, my_id, arg->ident->name, arg) == &freed)
			sql_insert_call_implies(PARAM_FREED, i, "$$", "1");
	} END_FOR_EACH_PTR(arg);
}

void check_frees_param(int id)
{
	my_id = id;

	add_hook(&match_function_def, FUNC_DEF_HOOK);

	if (option_project == PROJ_KERNEL) {
		add_function_hook("kfree", &match_free, INT_PTR(0));
		add_function_hook("kmem_cache_free", &match_free, INT_PTR(1));
	} else {
		add_function_hook("free", &match_free, INT_PTR(0));
	}

	select_call_implies_hook(PARAM_FREED, &set_param_freed);
	add_modification_hook(my_id, &set_ignore);

	all_return_states_hook(&process_states);
}
