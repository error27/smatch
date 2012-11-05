/*
 * sparse/check_err_ptr_deref.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(err_ptr);
STATE(checked);

static void ok_to_use(struct sm_state *sm)
{
	if (sm->state != &checked)
		set_state(my_id, sm->name, sm->sym, &checked);
}

static void check_is_err_ptr(struct sm_state *sm)
{
	if (!sm)
		return;

	if (slist_has_state(sm->possible, &err_ptr)) {
		sm_msg("error: '%s' dereferencing possible ERR_PTR()",
			   sm->name);
		set_state(my_id, sm->name, sm->sym, &checked);
	}
}

static void match_returns_err_ptr(const char *fn, struct expression *expr,
				void *info)
{
	set_state_expr(my_id, expr->left, &err_ptr);
}


static void match_checked(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *unused)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	arg = strip_expr(arg);
	while (arg->type == EXPR_ASSIGNMENT)
		arg = strip_expr(arg->left);
	set_state_expr(my_id, arg, &checked);
}

static void match_err(const char *fn, struct expression *call_expr,
			struct expression *assign_expr, void *unused)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(call_expr->args, 0);
	arg = strip_expr(arg);
	while (arg->type == EXPR_ASSIGNMENT)
		arg = strip_expr(arg->left);
	set_state_expr(my_id, arg, &err_ptr);
}

static void match_dereferences(struct expression *expr)
{
	struct sm_state *sm;

	if (expr->type != EXPR_PREOP)
		return;
	expr = strip_expr(expr->unop);

	sm = get_sm_state_expr(my_id, expr);
	check_is_err_ptr(sm);
}

static void match_condition(struct expression *expr)
{
	if (expr->type == EXPR_ASSIGNMENT) {
		match_condition(expr->right);
		match_condition(expr->left);
	}
	if (!get_state_expr(my_id, expr))
		return;
	/* If we know the variable is zero that means it's not an ERR_PTR */
	set_true_false_states_expr(my_id, expr, NULL, &checked);
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

static void match_err_ptr(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	struct sm_state *sm;
	struct sm_state *tmp;
	sval_t tmp_min;
	sval_t tmp_max;
	sval_t min = sval_type_max(&llong_ctype);
	sval_t max = sval_type_min(&llong_ctype);

	arg = get_argument_from_call_expr(expr->args, 0);
	sm = get_sm_state_expr(SMATCH_EXTRA, arg);
	if (!sm)
		return;
	FOR_EACH_PTR(sm->possible, tmp) {
		tmp_min = estate_min_sval(tmp->state);
		if (!sval_is_min(tmp_min) && sval_cmp(tmp_min, min) < 0)
			min = tmp_min;
		tmp_max = estate_max_sval(tmp->state);
		if (!sval_is_max(tmp_max) && sval_cmp(tmp_max, max) > 0)
			max = tmp_max;
	} END_FOR_EACH_PTR(tmp);
	if (sval_cmp_val(min, -4095) < 0)
		sm_msg("error: %s too low for ERR_PTR", sval_to_str(min));
	if (sval_cmp_val(max, 0) > 0)
		sm_msg("error: passing non neg %s to ERR_PTR", sval_to_str(max));
}

void check_err_ptr_deref(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	return_implies_state("IS_ERR", 0, 0, &match_checked, NULL);
	return_implies_state("IS_ERR", 1, 1, &match_err, NULL);
	return_implies_state("IS_ERR_OR_NULL", 0, 0, &match_checked, NULL);
	return_implies_state("IS_ERR_OR_NULL", 1, 1, &match_err, NULL);
	return_implies_state("PTR_RET", 0, 0, &match_checked, NULL);
	return_implies_state("PTR_RET", -4096, -1, &match_err, NULL);
	register_err_ptr_funcs();
	add_hook(&match_dereferences, DEREF_HOOK);
	add_function_hook("ERR_PTR", &match_err_ptr, NULL);
	add_hook(&match_condition, CONDITION_HOOK);
	add_modification_hook(my_id, &ok_to_use);
}

