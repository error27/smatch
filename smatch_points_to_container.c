/*
 * Copyright (C) 2022 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "ctype.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static char *handle_container_of_assign(struct expression *expr, struct expression **ptr)
{
	struct expression *right, *orig;
	struct symbol *type;
	sval_t sval;
	char buf[64];

	type = get_type(expr->left);
	if (!type || type->type != SYM_PTR)
		return NULL;

	right = strip_expr(expr->right);
	if (right->type != EXPR_BINOP || right->op != '-')
		return NULL;

	if (!get_value(right->right, &sval) ||
	   sval.value < 0 || sval.value > MTAG_OFFSET_MASK)
		return NULL;

	orig = get_assigned_expr(right->left);
	if (!orig)
		return NULL;

	snprintf(buf, sizeof(buf), "(%lld<~$)", sval.value);
	*ptr = orig;
	return alloc_string(buf);
}


static void match_assign(struct expression *expr)
{
	struct expression *ptr = NULL;
	char *container_str, *name;
	struct symbol *sym;
	char buf[64];

	if (expr->op != '=')
		return;

	if (is_fake_call(expr->right) || __in_fake_parameter_assign ||
	    __in_fake_struct_assign)
		return;

	container_str = handle_container_of_assign(expr, &ptr);
	if (!container_str || !ptr)
		return;
	name = expr_to_var_sym(ptr, &sym);
	if (!name || !sym)
		goto free;

	snprintf(buf, sizeof(buf), "%s %s", name, container_str);
	set_state(my_id, buf, sym, alloc_state_expr(expr->left));

free:
	free_string(name);
	free_string(container_str);
}

static void return_str_hook(struct expression *expr, const char *ret_str)
{
	struct expression *call, *arg;
	struct symbol *sym;
	int offset, param;
	char buf[32];
	char *name;

	if (!expr || expr->type != EXPR_ASSIGNMENT)
		return;
	call = expr;
	while (call && call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (!call || call->type != EXPR_CALL)
		return;

	if (!get_offset_param(ret_str, &offset, &param))
		return;

	arg = get_argument_from_call_expr(call->args, param);
	arg = strip_expr(arg);
	if (!arg)
		return;

	name = expr_to_var_sym(arg, &sym);
	if (!name || !sym)
		return;

	snprintf(buf, sizeof(buf), "%s (%d<~$)", name, offset);
	set_state(my_id, buf, sym, alloc_state_expr(expr->left));
}

struct expression *get_stored_container(struct expression *expr, int offset)
{
	struct smatch_state *state;
	struct symbol *sym;
	char buf[64];
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		return NULL;

	snprintf(buf, sizeof(buf), "%s (%d<~$)", name, offset);
	free_string(name);
	state = get_state(my_id, buf, sym);
	if (!state)
		return NULL;
	return state->data;
}

void register_points_to_container(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_hook(&match_assign, ASSIGNMENT_HOOK_AFTER);
	add_return_string_hook(return_str_hook);
	add_modification_hook(my_id, &set_undefined);
}

