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

struct deref_info {
	const char *name;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
};
static struct deref_info fn_deref_table[] = {
	{ "nla_data", 0, "$" },
	{ "strlen", 0, "$" },
	{ "__builtin_strlen", 0, "$" },
	{ "__fortify_strlen", 0, "$" },
	{ "spinlock_check", 0, "$" },
};

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

static bool is_array_pointer_math(struct expression *expr)
{
	struct expression *p, *parent;
	struct symbol *type;

	/* Ignore "&array[0]".  Fixme: wouldn't the & mean it was already ignored? */
	p = strip_expr(expr->unop);
	type = get_type(p);
	if (!type || type->type != SYM_PTR)
		return true;

	/* Ignore "p->array".  Here p is not actually dereferenced.  */
	parent = expr_get_parent_expr(expr);
	if (!parent)
		return false;

	type = get_type(parent);
	if (!type || type->type != SYM_ARRAY)
		return false;
	while ((parent = expr_get_parent_expr(parent))) {
		if (parent->type == EXPR_PREOP &&
		    parent->op == '*')
			return false;
	}

	return true;
}

static void match_dereference(struct expression *expr)
{
	struct expression *p, *tmp;
	struct symbol *type;

	if (expr->type != EXPR_PREOP ||
	    expr->op != '*')
		return;

	if (is_array_pointer_math(expr))
		return;

	p = strip_expr(expr->unop);
	call_deref_hooks(p);

	tmp = get_assigned_expr(p);
	if (!tmp)
		return;
	/*
	 * Imagine we have:
	 * p = &foo->bar;
	 * x = p->whatever;
	 *
	 * Note that we only care about address assignments because other
	 * dereferences would have been handled already.
	 */
	if (tmp->type != EXPR_PREOP || tmp->op != '&')
		return;
	p = strip_expr(tmp->unop);
	if (p->type != EXPR_DEREF)
		return;
	p = strip_expr(p->deref);
	if (p->type != EXPR_PREOP || p->op != '*')
		return;
	p = strip_expr(p->unop);
	type = get_type(p);
	if (!type || type->type != SYM_PTR)
		return;
	call_deref_hooks(p);
}

static void match_pointer_as_array(struct expression *expr)
{
	struct expression *array;
	struct symbol *type;

	if (!is_array(expr))
		return;
	if (getting_address(expr))
		return;

	array = get_array_base(expr);
	type = get_type(array);
	if (!type || type->type != SYM_PTR)
		return;

	call_deref_hooks(array);
}

static void dereference_inner_pointer(struct expression *expr)
{
	if (expr->type != EXPR_PREOP ||
	    expr->op != '&')
		return;
	expr = strip_expr(expr->unop);
	if (!expr || expr->type != EXPR_DEREF)
		return;
	expr = strip_expr(expr->unop);
	if (!expr || expr->type != EXPR_PREOP || expr->op != '*')
		return;
	expr = strip_expr(expr->unop);
	if (!expr)
		return;

	call_deref_hooks(expr);
}

static void set_param_dereferenced(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct expression *deref;

	deref = gen_expression_from_key(arg, key);
	if (!deref)
		return;

	call_deref_hooks(deref);

	/*
	 * Generally, this stuff should be handled by smatch_flow.c but
	 * smatch_flow.c doesn't have the PARAM_DEREFERENCED information so
	 * we go one level down to mark it as dereferenced.
	 *
	 */
	dereference_inner_pointer(deref);
}

static void param_deref(struct expression *expr)
{
	call_deref_hooks(expr);
}

void register_dereferences(int id)
{
	struct deref_info *info;
	int i;

	my_id = id;

	add_hook(&match_dereference, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_return_implies_hook_early(DEREFERENCE, &set_param_dereferenced);

	for (i = 0; i < ARRAY_SIZE(fn_deref_table); i++) {
		info = &fn_deref_table[i];
		add_param_key_expr_hook(info->name, &param_deref, info->param, info->key, info);
	}
}

