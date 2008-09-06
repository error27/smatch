/*
 * sparse/check_deference.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"

static int my_id;

static int malloc_size(struct expression *expr)
{
	char *name;
	struct expression *arg;

	if (!expr)
		return 0;

	if (expr->type == EXPR_CALL) {
		name = get_variable_from_expr_simple(expr->fn, NULL);
		if (name && !strcmp(name, "kmalloc")) {
			arg = get_argument_from_call_expr(expr->args, 0);
			return get_value(arg, NULL);
		}
	} else if (expr->type == EXPR_STRING && expr->string) {
		return expr->string->length * 8;
	}
	return 0;
}

static void match_declaration(struct symbol *sym)
{
	struct symbol *base_type;
	char *name;
	int size;

	if (!sym->ident)
		return;

	name = sym->ident->name;
	base_type = get_base_type(sym);
	
	if (base_type->type == SYM_ARRAY && base_type->bit_size > 0)
		set_state(name, my_id, NULL, base_type->bit_size / 8);
	else {
		size = malloc_size(sym->initializer);
		if (size > 0)
			set_state(name, my_id, NULL, size);
	}
}

static void match_assignment(struct expression *expr)
{
	char *name;
	name = get_variable_from_expr_simple(expr->left, NULL);
	name = alloc_string(name);
	if (!name)
		return;
	if (malloc_size(expr->right) > 0)
		set_state(name, my_id, NULL, malloc_size(expr->right));
}

static void match_fn_call(struct expression *expr)
{
	struct expression *dest;
	struct expression *data;
	char *fn_name;
	char *dest_name;
	char *data_name;

	fn_name = get_variable_from_expr(expr->fn, NULL);
	if (!fn_name)
		return;

	if (!strcmp(fn_name, "strcpy")) {
		dest = get_argument_from_call_expr(expr->args, 0);
		dest_name = get_variable_from_expr(dest, NULL);
		dest_name = alloc_string(dest_name);

		data = get_argument_from_call_expr(expr->args, 1);
		data_name = get_variable_from_expr(data, NULL);
		data_name = alloc_string(data_name);

		if (get_state(dest_name, my_id, NULL) < 0)
			return;
		if (get_state(dest_name, my_id, NULL)
		    < get_state(data_name, my_id, NULL))
		    smatch_msg("Error %s too large for %s", data_name, 
			       dest_name);
	} else if (!strcmp(fn_name, "strncpy")) {
		int needed;
		int has;

		dest = get_argument_from_call_expr(expr->args, 0);
		dest_name = get_variable_from_expr(dest, NULL);
		dest_name = alloc_string(dest_name);
		
		data = get_argument_from_call_expr(expr->args, 2);
		needed = get_value(data, NULL);
		has = get_state(dest_name, my_id, NULL);
		if (has > 0 && has < needed)
			smatch_msg("Error %s too small for %d bytes.", 
				   dest_name, needed);
	}
}

void register_overflow(int id)
{
	my_id = id;
	add_hook(&match_declaration, DECLARATION_HOOK);
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
	add_hook(&match_fn_call, FUNCTION_CALL_HOOK);
}
