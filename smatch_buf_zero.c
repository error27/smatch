/*
 * Copyright (C) 2012 Oracle.
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

/*
 * In theory this could have been tracked in smatch_buf_cleared.c but that
 * module was getting too complicated.
 */

#include "scope.h"
#include "smatch.h"

static int my_id;

STATE(zeroed);

static bool buf_contains(const char *container, const char *var, bool parent)
{
	bool addr = false;
	int i;

	/*
	 * What we do is:
	 * If "p" is memset then p->q and p->q.r are memset() but p->q->x is
	 * not.  If &arg is memset() then arg.a and arg.a.b are memset() but
	 * arg.a.b->c is not.
	 *
	 */

	if (container[0] == '&') {
		container++;
		addr = true;
	}

	i = 0;
	while (container[i] && container[i] == var[i])
		i++;

	if (container[i] != '\0')
		return false;

	var += i;
	if (var[0] == '\0')
		return true;
	if (var[0] != '.' && var[0] != '-')
		return false;

	if (parent)
		return true;

	if (!addr && var[0] == '-' && var[1] == '>')
		var += 2;

	if (strchr(var, '-'))
		return false;

	return true;
}

static bool ssa_buf_contains(const char *container, const char *var)
{
	int i;

	i = 0;
	while (container[i] && container[i] == var[i])
		i++;

	if (container[i] != '\0')
		return false;

	var += i;
	if (var[0] != '-')
		return false;
	return true;
}

static bool in_buf_zero_name_sym_helper(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	const char *ssa_name;

	if (!name)
		return false;

	if (!sym) {
		if (strchr(name, '{'))
			ssa_name = name;
		else
			return false;
	} else {
		ssa_name = get_ssa_ptr_name_sym(name, sym);
	}

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (ssa_name && sm->state == &zeroed &&
		    ssa_buf_contains(sm->name, ssa_name)) {
			return true;
		}
		if (sm->sym != sym)
			continue;
		if (sm->state != &zeroed)
			continue;
		if (buf_contains(sm->name, name, false))
			return true;
	} END_FOR_EACH_SM(sm);

	return false;
}

static bool in_buf_zero_helper(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	int ret;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return false;

	ret = in_buf_zero_name_sym_helper(name, sym);
	free_string(name);

	return ret;
}

bool in_buf_zero_name_sym(const char *name, struct symbol *sym)
{
	return in_buf_zero_name_sym_helper(name, sym);
}

bool in_buf_zero(struct expression *expr)
{
	return in_buf_zero_helper(expr);
}

static struct expression *remove_dereference(struct expression *expr)
{
	struct symbol *type;

	if (!expr)
		return NULL;

	if (expr->type == EXPR_PREOP && expr->op == '*')
		return strip_expr(expr->unop);

	type = get_type(expr);
	if (!type || type->type != SYM_STRUCT)
		return NULL;

	return preop_expression(expr, '&');
}

static struct expression *buf_zero_expr;
bool in_buf_zeroing_expr(void)
{
	struct expression *faked;

	faked = get_faked_expression();
	if (faked && faked == buf_zero_expr)
		return true;
	return false;
}

static void match_assign(struct expression *left, struct expression *right)
{
	struct expression *expr;

	if (!in_buf_zero(right))
		return;

	expr = remove_dereference(left);
	set_state_expr(my_id, expr, &zeroed);
}

static void match_zero_copy(int mode, struct expression *left, struct expression *right)
{
	struct expression *expr;

	if (mode != COPY_ZERO) {
		match_assign(left, right);
		return;
	}

	expr = remove_dereference(left);
	set_state_expr(my_id, expr, &zeroed);
	buf_zero_expr = get_faked_expression();
}

static void select_param_zeroed(const char *name, struct symbol *sym, char *value)
{
	set_state(my_id, name, sym, &zeroed);
}

static void caller_info_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (sm->state != &zeroed)
		return;
	sql_insert_caller_info(call, BUF_ZERO, param, printed_name, "");
}

void smatch_buf_zero(int id)
{
	my_id = id;

	add_modification_hook(my_id, &set_undefined);
	add_struct_copy_hook(match_zero_copy);
	add_caller_info_callback(my_id, caller_info_callback);
	select_caller_name_sym(&select_param_zeroed, BUF_ZERO);
}
