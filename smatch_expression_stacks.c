/*
 * sparse/smatch_expression_stacks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_expression_stacks.h"

void push_expression(struct expression_list **estack, struct expression *expr)
{
	add_ptr_list(estack, expr);
}

struct expression *pop_expression(struct expression_list **estack)
{
	struct expression *expr;

	expr = last_ptr_list((struct ptr_list *)*estack);
	delete_ptr_list_last((struct ptr_list **)estack);
	return expr;
}

struct expression *top_expression(struct expression_list *estack)
{
	struct expression *expr;

	expr = last_ptr_list((struct ptr_list *)estack);
	return expr;
}

void free_expression_stack(struct expression_list **estack)
{
	__free_ptr_list((struct ptr_list **)estack);
}
