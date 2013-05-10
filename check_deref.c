/*
 * sparse/check_deref.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * There was a previous null dereference test but it was too confusing and
 * difficult to debug.  This test is much simpler in its goals and scope.
 *
 * This test only complains about:
 * 1) dereferencing uninitialized variables
 * 2) dereferencing variables which were assigned as null.
 * 3) dereferencing variables which were assigned a function the returns 
 *    null.
 *
 * If we dereference something then we complain if any of those three
 * are possible.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

#define __GFP_NOFAIL 0x800

STATE(null);
STATE(ok);
STATE(uninitialized);

static struct smatch_state *alloc_my_state(const char *name)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = name;
	return state;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &ok;
}

static void is_ok(struct sm_state *sm)
{
	set_state(my_id, sm->name, sm->sym, &ok);
}

static void check_dereference(struct expression *expr)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	expr = strip_expr(expr);
	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return;
	if (is_ignored(my_id, sm->name, sm->sym))
		return;
	if (implied_not_equal(expr, 0))
		return;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->state == &merged)
			continue;
		if (tmp->state == &ok)
			continue;
		add_ignore(my_id, sm->name, sm->sym);
		if (tmp->state == &null) {
			if (option_spammy)
				sm_msg("error: potential NULL dereference '%s'.", tmp->name);
			return;
		}
		if (tmp->state == &uninitialized) {
			sm_msg("error: potentially dereferencing uninitialized '%s'.", tmp->name);
			return;
		}
		sm_msg("error: potential null dereference '%s'.  (%s returns null)",
			tmp->name, tmp->state->name);
		return;
	} END_FOR_EACH_PTR(tmp);
}

static void match_dereferences(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	check_dereference(expr->unop);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	check_dereference(expr->unop->left);
}

static void set_param_dereferenced(struct expression *arg, char *unused)
{
	check_dereference(arg);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if ((get_base_type(sym))->type == SYM_ARRAY)
		return;

	if (!sym->ident)
		return;
	name = sym->ident->name;
	if (!sym->initializer) {
		set_state(my_id, name, sym, &uninitialized);
		scoped_state(my_id, name, sym);
	}
}

static void match_assign(struct expression *expr)
{
	if (is_zero(expr->right)) {
		set_state_expr(my_id, expr->left, &null);
		return;
	}
}

static void match_condition(struct expression *expr)
{
	if (expr->type == EXPR_ASSIGNMENT) {
		match_condition(expr->right);
		match_condition(expr->left);
	}
	if (!get_state_expr(my_id, expr))
		return;
	set_true_false_states_expr(my_id, expr, &ok, NULL);
}

static int called_with_no_fail(struct expression *call, int param)
{
	struct expression *arg;
	sval_t sval;

	if (param == -1)
		return 0;
	call = strip_expr(call);
	if (call->type != EXPR_CALL)
		return 0;
	arg = get_argument_from_call_expr(call->args, param);
	if (get_value(arg, &sval) && (sval.uvalue & __GFP_NOFAIL))
		return 1;
	return 0;
}

static void match_assign_returns_null(const char *fn, struct expression *expr, void *_gfp)
{
	struct smatch_state *state;
	int gfp_param = PTR_INT(_gfp);

	if (called_with_no_fail(expr->right, gfp_param))
		return;
	state = alloc_my_state(fn);
	set_state_expr(my_id, expr->left, state);
}

static void register_allocation_funcs(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.allocation_funcs_gfp");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) == TOKEN_IDENT)
			arg = -1;
		else if (token_type(token) == TOKEN_NUMBER)
			arg = atoi(token->number);
		else
			return;
		add_function_assign_hook(func, &match_assign_returns_null, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_deref(int id)
{
	my_id = id;

	add_unmatched_state_hook(my_id, &unmatched_state);
	add_modification_hook(my_id, &is_ok);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	add_db_fn_call_callback(DEREFERENCE, &set_param_dereferenced);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	if (option_project == PROJ_KERNEL)
		register_allocation_funcs();
}
