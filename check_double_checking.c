/*
 * smatch/check_double_checking.c
 *
 * Copyright (C) 2014 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(checked);
STATE(modified);

struct state_list *to_check;

static void set_modified(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &modified);
}

static void match_condition(struct expression *expr)
{
	struct smatch_state *state;
	sval_t dummy;
	int is_true;
	char *name;

	if (__in_pre_post_condition)
		return;

	if (get_value(expr, &dummy))
		return;

	if (get_macro_name(expr->pos))
		return;

	// FIXME:  needed?
	if (implied_condition_true(expr))
		is_true = 1;
	else if (implied_condition_false(expr))
		is_true = 0;
	else
		return;

	state = get_stored_condition(expr);
	if (!state || !state->data)
		return;
	if (get_macro_name(((struct expression *)state->data)->pos))
		return;

	/*
	 * we allow double checking for NULL because people do this all the time
	 * and trying to stop them is a losers' battle.
	 */
	if (is_pointer(expr) && is_true)
		return;

	if (definitely_inside_loop()) {
		struct symbol *sym;

		if (__inline_fn)
			return;

		name = expr_to_var_sym(expr, &sym);
		if (!name)
			return;
		set_state_expr(my_id, expr, &checked);
		set_state_slist(&to_check, my_id, name, sym, &checked);
		free_string(name);
		return;
	}

	name = expr_to_str(expr);
	sm_msg("warn: we tested '%s' before and it was '%s'", name, state->name);
	free_string(name);
}

int get_check_line(struct sm_state *sm)
{
	struct sm_state *tmp;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &checked)
			return tmp->line;
	} END_FOR_EACH_PTR(tmp);

	return get_lineno();
}

static void after_loop(struct statement *stmt)
{
	struct sm_state *check, *sm;

	if (!stmt || stmt->type != STMT_ITERATOR)
		return;
	if (definitely_inside_loop())
		return;
	if (__inline_fn)
		return;

	FOR_EACH_PTR(to_check, check) {
		continue;
		sm = get_sm_state(my_id, check->name, check->sym);
		continue;
		if (!sm)
			continue;
		if (slist_has_state(sm->possible, &modified))
			continue;

		sm_printf("%s:%d %s() ", get_filename(), get_check_line(sm), get_function());
		sm_printf("warn: we tested '%s' already\n", check->name);
	} END_FOR_EACH_PTR(check);

	free_slist(&to_check);
}

static void match_func_end(struct symbol *sym)
{
	if (__inline_fn)
		return;
	if (to_check)
		sm_msg("debug: odd...  found an function without an end.");
	free_slist(&to_check);
}

void check_double_checking(int id)
{
	my_id = id;

	if (!option_spammy)
		return;

	add_hook(&match_condition, CONDITION_HOOK);
	add_modification_hook(my_id, &set_modified);
	add_hook(after_loop, STMT_HOOK_AFTER);
	add_hook(&match_func_end, END_FUNC_HOOK);
}
