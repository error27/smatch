/*
 * sparse/check_dev_queue_xmit.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * According to an email on lkml you are not allowed to reuse the skb
 * passed to dev_queue_xmit()
 *
 */

#include "smatch.h"

static int my_id;

STATE(do_not_use);

static void ok_to_use(struct sm_state *sm)
{
	delete_state(my_id, sm->name, sm->sym);
}

static int valid_use(void)
{
	struct expression *tmp;
	int i = 0;
	int dot_ops = 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (!i++)
			continue;
		if (tmp->type == EXPR_PREOP && tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
//		if (tmp->type == EXPR_POSTOP)
//			return 1;
		if (tmp->type == EXPR_CALL && sym_name_is("kfree_skb", tmp->fn))
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

/* match symbol is expensive.  only turn it on after we match the xmit function */
static int match_symbol_active;
static void match_symbol(struct expression *expr)
{
	struct sm_state *sm;
	char *name;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return;
	if (valid_use())
		return;
	name = get_variable_from_expr(expr, NULL);
	sm_msg("error: '%s' was already used up by dev_queue_xmit()", name);
	free_string(name);
}

static void match_kfree_skb(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, 0);
	if (!arg)
		return;
	delete_state_expr(my_id, arg);
}

static void match_xmit(const char *fn, struct expression *expr, void *param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(param));
	if (!arg)
		return;
	set_state_expr(my_id, arg, &do_not_use);
	if (!match_symbol_active++) {
		add_hook(&match_symbol, SYM_HOOK);
		add_function_hook("kfree_skb", &match_kfree_skb, NULL);
	}
}

static void register_funcs_from_file(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.dev_queue_xmit");
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
		if (token_type(token) != TOKEN_NUMBER)
			return;
		arg = atoi(token->number);
		add_function_hook(func, &match_xmit, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_dev_queue_xmit(int id)
{
	if (option_project != PROJ_KERNEL || !option_spammy)
		return;
	my_id = id;
	add_modification_hook(my_id, ok_to_use);
	register_funcs_from_file();
}
