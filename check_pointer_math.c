/*
 * smatch/check_pointer_math.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

static int my_id;

STATE(size_in_bytes);

static void set_undefined(struct sm_state *sm)
{
	if (sm->state == &size_in_bytes)
		set_state(my_id, sm->name, sm->sym, &undefined);
}

static int is_sizeof(struct expression *expr)
{
	return (expr->type == EXPR_SIZEOF);
}

static int is_align(struct expression *expr)
{
	char *name;
	struct expression *outside_expr;

	/* check that we aren't inside the ALIGN() macro itself */
	outside_expr = last_ptr_list((struct ptr_list *)big_expression_stack);
	if (outside_expr && positions_eq(expr->pos, outside_expr->pos))
		return 0;

	name = get_macro_name(expr->pos);
	if (name && strcmp(name, "ALIGN") == 0)
		return 1;
	return 0;
}

static int is_size_in_bytes(struct expression *expr)
{
	if (is_sizeof(expr))
		return 1;

	if (is_align(expr))
		return 1;

	if (get_state_expr(my_id, expr) == &size_in_bytes)
		return 1;

	return 0;
}

static void match_binop(struct expression *expr)
{
	struct symbol *type;
	char *name;

	if (expr->op != '+')
		return;
	type = get_pointer_type(expr->left);
	if (!type)
		return;
	if (type->bit_size <= 8) /* ignore void, bool and char pointers*/
		return;
	if (!is_size_in_bytes(expr->right))
		return;

	name = get_variable_from_expr_complex(expr->left, NULL);
	sm_msg("warn: potential pointer math issue ('%s' is a %d bit pointer)",
	       name, type->bit_size);
	free_string(name);
}

static void match_assign(struct expression *expr)
{
	if (expr->op != '=')
		return;

	if (!is_size_in_bytes(expr->right))
		return;
	set_state_expr(my_id, expr->left, &size_in_bytes);
}

void check_pointer_math(int id)
{
	my_id = id;
	add_hook(&match_binop, BINOP_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_modification_hook(my_id, &set_undefined);
}
