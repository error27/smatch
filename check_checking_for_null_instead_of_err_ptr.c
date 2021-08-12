/*
 * Copyright (C) 2017 Oracle.
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

STATE(err_ptr);

static void ok_to_use(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

static void match_returns_err_ptr(const char *fn, struct expression *expr,
				void *info)
{
	set_state_expr(my_id, expr->left, &err_ptr);
}

static void match_condition(struct expression *expr)
{
	struct sm_state *sm;
	struct range_list *rl;
	sval_t zero = {
		.type = NULL,
		{ .value = 0, }
	};
	char *name;
	struct range_list *err_rl;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->left);

	if (!is_pointer(expr))
		return;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return;
	if (!slist_has_state(sm->possible, &err_ptr))
		return;
	if (!get_implied_rl(expr, &rl))
		return;
	zero.type = rl_type(rl);
	if (rl_has_sval(rl, zero))
		return;

	/*
	 * At this point we already know that the condition is bogus because
	 * it's non-NULL.  But let's do another filter to make sure it really is
	 * a possible error pointer.
	 *
	 */

	err_rl = alloc_rl(err_min, err_max);
	if (!possibly_true_rl(rl, SPECIAL_EQUAL, err_rl))
		return;

	name = expr_to_str(expr);
	sm_msg("warn: '%s' is an error pointer or valid", name);
	free_string(name);
}

static void match_condition2(struct expression *expr)
{
	struct range_list *rl;
	struct data_range *drange;
	char *name;

	if (!is_pointer(expr))
		return;
	if (!get_implied_rl(expr, &rl))
		return;

	FOR_EACH_PTR(rl, drange) {
		if (sval_cmp(drange->min, drange->max) != 0)
			continue;
		if (drange->min.value >= -4095 && drange->min.value < 0)
			goto warn;
	} END_FOR_EACH_PTR(drange);

	return;

warn:
	name = expr_to_str(expr);
	if (option_spammy)
		sm_warning("'%s' could be an error pointer", name);
	free_string(name);
}

static void register_err_ptr_funcs(void)
{
	struct token *token;
	const char *func;

	token = get_tokens_file("kernel.returns_err_ptr");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		add_function_assign_hook(func, &match_returns_err_ptr, NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void check_checking_for_null_instead_of_err_ptr(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	register_err_ptr_funcs();
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_condition2, CONDITION_HOOK);
	add_modification_hook(my_id, &ok_to_use);
}

