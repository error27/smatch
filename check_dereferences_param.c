/*
 * sparse/check_dereferences_param.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(derefed);
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

static void set_ignore(struct sm_state *sm)
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

static void check_deref(struct expression *expr)
{
	struct sm_state *sm;

	expr = strip_expr(expr);

	if (!is_arg(expr))
		return;
	if (implied_not_equal(expr, 0))
		return;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &ignore))
		return;
	set_state_expr(my_id, expr, &derefed);
}

static void match_dereference(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	if (getting_address())
		return;
	check_deref(expr->unop);
}

static void set_param_dereferenced(struct expression *arg, char *unused)
{
	check_deref(arg);
}

static void process_states(struct state_list *slist)
{
	struct symbol *arg;
	int i;

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		i++;
		if (!arg->ident)
			continue;
		if (get_state_slist(slist, my_id, arg->ident->name, arg) == &derefed)
			sm_msg("info: dereferences_param %d", i);
	} END_FOR_EACH_PTR(arg);
}

void check_dereferences_param(int id)
{
	if (!option_info)
		return;

	my_id = id;

	add_hook(&match_function_def, FUNC_DEF_HOOK);

	add_hook(&match_dereference, DEREF_HOOK);
	add_db_fn_call_callback(DEREFERENCE, &set_param_dereferenced);
	add_modification_hook(my_id, &set_ignore);

	all_return_states_hook(&process_states);
}
