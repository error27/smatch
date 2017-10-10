/*
 * Copyright (C) 2015 Oracle.
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
#include "smatch_slist.h"

static int my_id;

struct stree *used_stree;
STATE(used);

static int get_container_arg(struct symbol *sym)
{
	struct expression *__mptr, *param_expr;
	sval_t sval;
	static int recursion;
	int ret = -1;

	if (recursion)
		return false;
	recursion = 1;

	if (!sym || !sym->ident)
		goto out;
	__mptr = get_assigned_expr_name_sym(sym->ident->name, sym);
	if (!__mptr)
		goto out;
	__mptr = strip_expr(__mptr);
	if (__mptr->type != EXPR_BINOP || __mptr->op != '-')
		goto out;

	if (!get_value(__mptr->right, &sval))
		goto out;

	param_expr = get_assigned_expr(__mptr->left);
	if (!param_expr)
		goto out;

	ret = get_param_num(param_expr);
out:
	recursion = 0;
	return ret;
}

static int get_container_offset(struct symbol *sym)
{
	struct expression *__mptr, *param_expr;
	sval_t sval;
	static int recursion;
	int ret = -1;
	int param;

	if (recursion)
		return false;
	recursion = 1;

	if (!sym || !sym->ident)
		goto out;
	__mptr = get_assigned_expr_name_sym(sym->ident->name, sym);
	if (!__mptr)
		goto out;
	__mptr = strip_expr(__mptr);
	if (__mptr->type != EXPR_BINOP || __mptr->op != '-')
		goto out;

	if (!get_value(__mptr->right, &sval))
		goto out;
	if (sval.value < 0 || sval.value > 4096)
		goto out;

	param_expr = get_assigned_expr(__mptr->left);
	if (!param_expr)
		goto out;

	param = get_param_num(param_expr);
	if (param < 0)
		goto out;

	ret = sval.value;
out:
	recursion = 0;
	return ret;
}

static char *get_container_name(struct sm_state *sm, int offset)
{
	static char buf[256];
	const char *name;

	name = get_param_name(sm);
	if (!name)
		return NULL;

	if (name[0] == '$')
		snprintf(buf, sizeof(buf), "$(-%d)%s", offset, name + 1);
	else if (name[0] == '*' || name[1] == '$')
		snprintf(buf, sizeof(buf), "*$(-%d)%s", offset, name + 2);
	else
		return NULL;

	return buf;
}

/*
 * I am a loser.  This should be a proper hook, but I am too lazy.  I'll add
 * that stuff if we have a second user.
 */
void __get_state_hook(int owner, const char *name, struct symbol *sym)
{
	int arg;

	if (!option_info)
		return;
	if (__in_fake_assign)
		return;

	arg = get_container_arg(sym);
	if (arg >= 0)
		goto save;

	arg = get_param_num_from_sym(sym);
	if (arg < 0)
		return;

save:
	set_state_stree(&used_stree, my_id, name, sym, &used);
}

static void set_param_used(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;
	int arg_nr;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	arg_nr = get_container_arg(sym);
	if (arg_nr >= 0)
		goto save;

	arg_nr = get_param_num_from_sym(sym);
	if (arg_nr < 0)
		goto free;

save:
	set_state(my_id, name, sym, &used);
free:
	free_string(name);
}

static void process_states(void)
{
	struct sm_state *tmp;
	int arg, offset;
	const char *name;

	FOR_EACH_SM(used_stree, tmp) {
		arg = get_param_num_from_sym(tmp->sym);
		if (arg >= 0) {
			name = get_param_name(tmp);
			if (!name)
				continue;
			goto insert;
		}

		arg = get_container_arg(tmp->sym);
		offset = get_container_offset(tmp->sym);
		if (arg < 0 || offset < 0)
			continue;
		name = get_container_name(tmp, offset);
		if (!name)
			continue;
insert:
		sql_insert_call_implies(PARAM_USED, arg, name, "");
	} END_FOR_EACH_SM(tmp);

	free_stree(&used_stree);
}

void register_param_used(int id)
{
	my_id = id;

	if (!option_info)
		return;

	select_call_implies_hook(PARAM_USED, &set_param_used);
	all_return_states_hook(&process_states);
}
