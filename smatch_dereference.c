/*
 * Copyright (C) 2021 Oracle.
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

#include "smatch.h"
#include "smatch_extra.h"

static int my_id;
static struct expr_fn_list *deref_hooks;

void add_dereference_hook(expr_func *fn)
{
	add_ptr_list(&deref_hooks, fn);
}

static void call_deref_hooks(struct expression *expr)
{
	if (__in_fake_assign || __in_fake_parameter_assign)
		return;
	if (!expr)
		return;

	call_expr_fns(deref_hooks, expr);
}

static void match_dereference(struct expression *expr)
{
	if (expr->type != EXPR_PREOP ||
	    expr->op != '*')
		return;
	expr = strip_expr(expr->unop);
	if (!is_pointer(expr))
		return;
	call_deref_hooks(expr);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	call_deref_hooks(get_array_base(expr));
}

static void set_param_dereferenced(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct expression *deref;
	struct symbol *sym;
	char *name;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;
	if (name[0] == '&')
		goto free;

	deref = gen_expression_from_name_sym(name, sym);
	if (!deref)
		goto free;

	call_deref_hooks(deref);
free:
	free_string(name);
}

void register_dereferences(int id)
{
	my_id = id;

	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_return_implies_hook(DEREFERENCE, &set_param_dereferenced);
}

