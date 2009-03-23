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

static struct smatch_state *alloc_state(int val)
{
	struct smatch_state *state;

	state = malloc(sizeof(*state));
	state->name = "value";
	state->data = malloc(sizeof(int));
	*(int *)state->data = val;
	return state;
}

static int malloc_size(struct expression *expr)
{
	char *name;
	struct expression *arg;

	if (!expr)
		return 0;

	if (expr->type == EXPR_CALL) {
		name = get_variable_from_expr(expr->fn, NULL);
		if (name && !strcmp(name, "kmalloc")) {
			arg = get_argument_from_call_expr(expr->args, 0);
			free_string(name);
			return get_value(arg);
		}
		free_string(name);
	} else if (expr->type == EXPR_STRING && expr->string) {
		return expr->string->length;
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
	
	if (base_type->type == SYM_ARRAY && base_type->bit_size > 0) {
		set_state(name, my_id, NULL, alloc_state(base_type->bit_size / 8));
	} else {
		size = malloc_size(sym->initializer);
		if (size > 0)
			set_state(name, my_id, NULL, alloc_state(size));

	}
}

static void match_assignment(struct expression *expr)
{
	char *name;
	name = get_variable_from_expr(expr->left, NULL);
	if (!name)
		return;
	if (malloc_size(expr->right) > 0)
		set_state(name, my_id, NULL, alloc_state(malloc_size(expr->right)));
	free_string(name);
}

static void match_strcpy(struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	char *data_name = NULL;
	struct smatch_state *dest_state;
	struct smatch_state *data_state;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, 1);
	data_name = get_variable_from_expr(data, NULL);
		
	dest_state = get_state(dest_name, my_id, NULL);
	if (!dest_state || !dest_state->data)
		goto free;

	data_state = get_state(data_name, my_id, NULL);
	if (!data_state || !data_state->data)
		goto free;

	if (*(int *)dest_state->data < *(int *)data_state->data)
		smatch_msg("error: %s (%d) too large for %s (%d)", data_name,
			   *(int *)data_state->data,
			   dest_name, *(int *)dest_state->data);
free:
	free_string(dest_name);
	free_string(data_name);
}

static void match_strncpy(struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	struct smatch_state *state;
	int needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, 2);
	needed = get_value(data);
	state = get_state(dest_name, my_id, NULL);
	if (!state || !state->data)
		goto free;
	has = *(int *)state->data;
	if (has < needed)
		smatch_msg("error: %s too small for %d bytes.", dest_name,
			   needed);
free:
	free_string(dest_name);
}

void check_overflow(int id)
{
	my_id = id;
	add_hook(&match_declaration, DECLARATION_HOOK);
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_strncpy, NULL);
}
