/*
 * smatch/check_bit_shift.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_function_hashtable.h"

static int my_id;

static DEFINE_HASHTABLE_INSERT(insert_struct, char, int);
static DEFINE_HASHTABLE_SEARCH(search_struct, char, int);
static struct hashtable *shifters;

static const char *get_shifter(struct expression *expr)
{
	const char *name;
	long long expr_value;
	const int *shifter_value;

	expr = strip_expr(expr);
	if (expr->type != EXPR_VALUE)
		return NULL;
	if (!get_value(expr, &expr_value))
		return NULL;
	name = pos_ident(expr->pos);
	if (!name)
		return NULL;
	shifter_value = search_struct(shifters, (char *)name);
	if (!shifter_value)
		return NULL;
	if (*shifter_value != expr_value)
		return NULL;
	return name;
}

static void match_assign(struct expression *expr)
{
	const char *name;

	if (expr->op != SPECIAL_OR_ASSIGN)
		return;
	if (positions_eq(expr->pos, expr->right->pos))
		return;
	name = get_shifter(expr->right);
	if (!name)
		return;

	sm_msg("warn: '%s' is a shifter (not for '%s').",
			name, show_special(expr->op));
}

static void match_binop(struct expression *expr)
{
	const char *name;

	if (positions_eq(expr->pos, expr->right->pos))
		return;
	if (expr->op != '&')
		return;
	name = get_shifter(expr->right);
	if (!name)
		return;

	sm_msg("warn: bit shifter '%s' used for logical '%s'",
			name, show_special(expr->op));
}

static void register_shifters(void)
{
	char filename[256];
	struct token *token;
	char *name;
	int *val;

	snprintf(filename, sizeof(filename), "%s.bit_shifters", option_project_str);
	token = get_tokens_file(filename);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		name = alloc_string(show_ident(token->ident));
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		val = malloc(sizeof(int));
		*val = atoi(token->number);
		insert_struct(shifters, name, val);
		token = token->next;
	}
	clear_token_alloc();
}

static void match_binop_info(struct expression *expr)
{
	char *name;
	long long val;

	if (positions_eq(expr->pos, expr->right->pos))
		return;
	if (expr->op != SPECIAL_LEFTSHIFT)
		return;
	if (expr->right->type != EXPR_VALUE)
		return;
	name = pos_ident(expr->right->pos);
	if (!name)
		return;
	if (!get_value(expr->right, &val))
		return;
	sm_msg("info: bit shifter '%s' '%lld'", name, val);
}

static void match_call(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *arg_expr;
	int arg_no = PTR_INT(_arg_no);
	long long val;
	char *name;

	arg_expr = get_argument_from_call_expr(expr->args, arg_no);
	if (positions_eq(expr->pos, arg_expr->pos))
		return;
	name = pos_ident(arg_expr->pos);
	if (!name)
		return;
	if (!get_value(arg_expr, &val))
		return;
	sm_msg("info: bit shifter '%s' '%lld'", name, val);
}

void check_bit_shift(int id)
{
	my_id = id;

	shifters = create_function_hashtable(5000);
	register_shifters();

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_binop, BINOP_HOOK);

	if (option_info) {
		add_hook(&match_binop_info, BINOP_HOOK);
		if (option_project == PROJ_KERNEL)
			add_function_hook("set_bit", &match_call, INT_PTR(0));
	}
}
