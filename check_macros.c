/*
 * smatch/check_macros.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static int same_pos(struct position *pos1, struct position *pos2)
{
	if (pos1->stream != pos2->stream)
		return 0;
	if (pos1->line != pos2->line)
		return 0;
	if (pos1->pos != pos2->pos)
		return 0;
	return 1;
}

static void match_inside(struct expression *expr, struct position *pos)
{
	char *name;
	int matched = 0;

	if (same_pos(&expr->pos, pos))
		matched++;
	if (same_pos(&expr->unop->pos, pos))
		matched++;
	if (matched != 1)
		return;
	name = get_macro_name(pos);
	if (!name)
		return;
	sm_msg("warn: the '%s' macro might need parens", name);
}

static void match_one_side(struct expression *expr, struct position *pos, int op)
{
	char *name;
	int matched = 0;

	if ((op == '+' || op == '*' || op == '|' || op == '&') && expr->op == op)
		return;
	if (same_pos(&expr->right->pos, pos))
		matched++;
	if (same_pos(&expr->left->pos, pos))
		matched++;
	if (matched != 1)
		return;
	name = get_macro_name(pos);
	if (!name)
		return;
	if (option_project == PROJ_WINE && !strcmp("BEGIN", name))
		return;
	sm_msg("warn: the '%s' macro might need parens", name);
}

static void match_join(struct expression *expr)
{
	if (expr->left->type == EXPR_PREOP)
		match_inside(expr->left, &expr->pos);
	if (expr->right->type == EXPR_POSTOP)
		match_inside(expr->right, &expr->pos);

	if (expr->left->type == EXPR_BINOP)
		match_one_side(expr->left, &expr->pos, expr->op);
	if (expr->right->type == EXPR_BINOP)
		match_one_side(expr->right, &expr->pos, expr->op);
}

void check_macros(int id)
{
	my_id = id;
	add_hook(&match_join, BINOP_HOOK);
	add_hook(&match_join, LOGIC_HOOK);
}
