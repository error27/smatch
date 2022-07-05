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

/*
 * The point here is to store that a buffer has x bytes even if we don't know
 * the value of x.
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

bool buf_comp2_has_bytes(struct expression *buf_expr, struct expression *var)
{
	char *buffer_name, *var_name;
	bool ret = false;
	int comparison;
	char buf[64];

	buffer_name = expr_to_var(buf_expr);
	if (!buffer_name)
		return false;
	var_name = expr_to_var(var);
	if (!var_name)
		goto free;
	snprintf(buf, sizeof(buf), "$size %s", buffer_name);
	comparison = get_comparison_strings(buf, var_name);
	if (!comparison)
		goto free;

	if (comparison == SPECIAL_EQUAL ||
	    show_special(comparison)[0] == '>')
		ret = true;

free:
	free_string(buffer_name);
	free_string(var_name);
	return ret;
}

static void record_size(struct expression *buffer, struct expression *size, struct expression *mod_expr)
{
	struct var_sym_list *buffer_vsl, *size_vsl;
	char *buffer_name, *size_name;
	char buf[64];

	buffer_name = expr_to_chunk_sym_vsl(buffer, NULL, &buffer_vsl);
	size_name = expr_to_chunk_sym_vsl(size, NULL, &size_vsl);
	if (!buffer_name || !size_name) {
		free_string(buffer_name);
		free_string(size_name);
		return;
	}

	snprintf(buf, sizeof(buf), "$size %s", buffer_name);
	free_string(buffer_name);
	add_comparison_var_sym(buffer, buf, buffer_vsl, SPECIAL_EQUAL,
			       size, size_name, size_vsl, mod_expr);
}

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	struct expression *call, *size_arg;
	sval_t sval;

	/* FIXME: hack for testing */
	if (strcmp(info->size_str, "$0") != 0)
		return;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;

	call = expr;
	while (call && call->type == EXPR_ASSIGNMENT)
		call = strip_expr(expr->right);
	if (!call || call->type != EXPR_CALL)
		return;

	size_arg = get_argument_from_call_expr(call->args, 0);
	if (!size_arg)
		return;

	if (get_implied_value(size_arg, &sval))
		return;

	record_size(expr->left, size_arg, expr);
}

static int get_param(int param, char **name, struct symbol **sym)
{
	struct symbol *arg;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		if (i == param) {
			*name = arg->ident->name;
			*sym = arg;
			return TRUE;
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	return FALSE;
}

static void set_param_compare(const char *buffer_name, struct symbol *buffer_sym, char *key, char *value)
{
	struct expression *buffer, *size;
	struct symbol *size_sym;
	char *size_name;
	long param;

	if (strncmp(key, "==$", 3) != 0)
		return;
	param = strtol(key + 3, NULL, 10);
	if (!get_param(param, &size_name, &size_sym))
		return;
	buffer = symbol_expression(buffer_sym);
	size = symbol_expression(size_sym);

	record_size(buffer, size, NULL);
}

void register_buf_comparison2(int id)
{
	my_id = id;

	add_allocation_hook(&match_allocation);
	select_caller_info_hook(set_param_compare, BYTE_COUNT);
}

