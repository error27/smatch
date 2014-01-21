/*
 * smatch/smatch_unknown_value.c
 *
 * Copyright (C) 2014 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The situation here is that we often need to fake an assignment but we don't
 * know anything about the right hand side of the assignment.  We use a fake
 * function call of &llong_ctype.  The reason for using a function call instead
 * of a value is so we don't start storing the equivalence.
 *
 */

#include "smatch.h"

struct ident fake_assign = {
	.len = sizeof("fake assign"),
	.name = "fake assign",
};

static struct symbol fake_fn_symbol = {
	.type = SYM_FN,
	.ident = &fake_assign,
	.ctype =  {
		.base_type = &llong_ctype,
	},
};

static struct symbol fake_node_symbol = {
	.type = SYM_NODE,
	.ident = &fake_assign,
	.ctype = {
		.base_type = &fake_fn_symbol,
	},
};

static struct expression fake_fn_expr = {
	.type = EXPR_SYMBOL,
	.ctype = &llong_ctype,
	.symbol = &fake_node_symbol,
	.symbol_name = &fake_assign,
};

static struct expression fake_call = {
	.type = EXPR_CALL,
	.ctype = &llong_ctype,
	.fn = &fake_fn_expr,
};

struct expression *unknown_value_expression(struct expression *expr)
{
	return &fake_call;
}

int is_fake_call(struct expression *expr)
{
	return expr == &fake_call;
}
