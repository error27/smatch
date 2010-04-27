/*
 * sparse/check_no_effect.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

static void match_stmt(struct statement *stmt)
{
	struct expression *expr;

	if (stmt->type != STMT_EXPRESSION)
		return;
	expr = stmt->expression;
	if (!expr)
		return;
	switch(expr->type) {
	case EXPR_PREOP:
	case EXPR_POSTOP:
	case EXPR_STATEMENT:
	case EXPR_ASSIGNMENT:
	case EXPR_CALL:
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_COMMA:
		return;
	}
	if (in_expression_statement())
		return;
	sm_msg("warn: statement has no effect %d", expr->type);
}

void check_no_effect(int id)
{
	my_id = id;
	add_hook(&match_stmt, STMT_HOOK);
}
