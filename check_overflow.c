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
#include "smatch_slist.h"

static int my_id;

static char *alloc_num(long long num)
{
	static char buff[256];

	if (num == whole_range.min) {
		snprintf(buff, 255, "min");
	} else if (num == whole_range.max) {
		snprintf(buff, 255, "max");
	} else if (num < 0) {
		snprintf(buff, 255, "(%lld)", num);
	} else {
		snprintf(buff, 255, "%lld", num);
	}
	buff[255] = '\0';
	return alloc_sname(buff);
}

static struct smatch_state *alloc_my_state(int val)
{
	struct smatch_state *state;

	state = malloc(sizeof(*state));
	state->name = alloc_num(val);
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

	expr = strip_expr(expr);
	if (expr->type == EXPR_CALL) {
		name = get_variable_from_expr(expr->fn, NULL);
		if (name && !strcmp(name, "kmalloc")) {
			arg = get_argument_from_call_expr(expr->args, 0);
			free_string(name);
			return get_value(arg) * 8;
		}
		free_string(name);
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
	
	if (base_type->type == SYM_ARRAY && base_type->bit_size > 0) {
		set_state(name, my_id, NULL, alloc_my_state(base_type->bit_size));
	} else {
		size = malloc_size(sym->initializer);
		if (size > 0)
			set_state(name, my_id, NULL, alloc_my_state(size));

	}
}

static int get_array_size(struct expression *expr)
{
	char *name;
	struct symbol *tmp;
	struct smatch_state *state;
	int ret = 0;

	if (expr->type != EXPR_SYMBOL)
		return 0;
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return 0;
	state = get_state(name, my_id, NULL);
	if (!state || !state->data)
		goto free;
	tmp = get_base_type(expr->symbol);
	if (tmp->type == SYM_PTR)
		tmp = get_base_type(tmp);
	ret = *(int *)state->data / 8 / tmp->ctype.alignment;
free:
	free_string(name);
	return ret;
}

static void array_check(struct expression *expr)
{
	struct expression *dest;
	int array_size;
	struct expression *offset;
	int max;
	char *name;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	dest = get_array_name(expr);
	array_size = get_array_size(dest);
	if (!array_size)
		return;

	offset = get_array_offset(expr);
	max = get_implied_max(offset);
	if (array_size <= max) {
		name = get_variable_from_expr(dest, NULL);
		smatch_msg("error: buffer overflow '%s' %d <= %d", name, array_size, max);
		free_string(name);
	}
}

static void match_assignment(struct expression *expr)
{
	char *name;
	name = get_variable_from_expr(expr->left, NULL);
	if (!name)
		return;
	if (malloc_size(expr->right) > 0)
		set_state(name, my_id, NULL, alloc_my_state(malloc_size(expr->right)));
	free_string(name);
}

static void match_strcpy(const char *fn, struct expression *expr,
			 void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	char *data_name = NULL;
	struct smatch_state *dest_state;
	struct smatch_state *data_state;
	int dest_size;
	int data_size;

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
	dest_size = *(int *)dest_state->data / 8;
	data_size = *(int *)data_state->data / 8;
	if (dest_size < data_size)
		smatch_msg("error: %s (%d) too large for %s (%d)", data_name,
			   data_size, dest_name, dest_size);
free:
	free_string(dest_name);
	free_string(data_name);
}

static void match_limitted(const char *fn, struct expression *expr,
			   void *limit_arg)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	struct smatch_state *state;
	int needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, PTR_INT(limit_arg));
	needed = get_value(data);
	state = get_state(dest_name, my_id, NULL);
	if (!state || !state->data)
		goto free;
	has = *(int *)state->data / 8;
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
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_assignment, ASSIGNMENT_HOOK);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_limitted, (void *)2);
	add_function_hook("copy_to_user", &match_limitted, (void *)2);
	add_function_hook("copy_from_user", &match_limitted, (void *)2);
}
