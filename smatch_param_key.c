/*
 * Copyright (C) 2020 Oracle.
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

int get_param_key_from_var_sym(const char *name, struct symbol *sym,
			       struct expression *ret_expr,
			       const char **key)
{
	char *other_name;
	struct symbol *other_sym;
	const char *param_name;
	int param;

	*key = name;

	/* straight forward param match */
	param = get_param_num_from_sym(sym);
	if (param >= 0) {
		param_name = get_param_name_var_sym(name, sym);
		if (param_name)
			*key = param_name;
		return param;
	}

	/* it's the return value */
	if (ret_expr) {
		struct symbol *ret_sym;
		char *ret_str;

		ret_str = expr_to_str_sym(ret_expr, &ret_sym);
		if (ret_str && ret_sym == sym) {
			param_name = state_name_to_param_name(name, ret_str);
			if (param_name) {
				free_string(ret_str);
				*key = param_name;
				return -1;
			}
		}
		free_string(ret_str);
	}

	/* it is an assigned parameter */
	other_name = get_other_name_sym(name, sym, &other_sym);
	if (!other_name)
		return -2;
	param = get_param_num_from_sym(other_sym);
	if (param < 0)
		return -2;

	param_name = get_param_name_var_sym(other_name, other_sym);
	free_string(other_name);
	if (param_name)
		*key = param_name;
	return param;
}

int get_param_key_from_sm(struct sm_state *sm, struct expression *ret_expr,
			  const char **key)
{
	return get_param_key_from_var_sym(sm->name, sm->sym, ret_expr, key);
}

int get_param_num_from_sym(struct symbol *sym)
{
	struct symbol *tmp;
	int i;

	if (!sym)
		return UNKNOWN_SCOPE;

	if (sym->ctype.modifiers & MOD_TOPLEVEL) {
		if (sym->ctype.modifiers & MOD_STATIC)
			return FILE_SCOPE;
		return GLOBAL_SCOPE;
	}

	if (!cur_func_sym) {
		if (!parse_error) {
			sm_msg("warn: internal.  problem with scope:  %s",
			       sym->ident ? sym->ident->name : "<anon var>");
		}
		return GLOBAL_SCOPE;
	}


	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(tmp);
	return LOCAL_SCOPE;
}

int get_param_num(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (!cur_func_sym)
		return UNKNOWN_SCOPE;
	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!sym)
		return UNKNOWN_SCOPE;
	return get_param_num_from_sym(sym);
}

struct symbol *get_param_sym_from_num(int num)
{
	struct symbol *sym;
	int i;

	if (!cur_func_sym)
		return NULL;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, sym) {
		if (i++ == num)
			return sym;
	} END_FOR_EACH_PTR(sym);
	return NULL;
}

void register_param_key(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
}

