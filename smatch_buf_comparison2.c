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

static int my_id;

static struct smatch_state *alloc_expr_state(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	expr = strip_expr(expr);
	name = expr_to_str(expr);
	if (!name)
		return NULL;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(name);
	free_string(name);
	state->data = expr;
	return state;
}

struct smatch_state *merge_expr_states(struct smatch_state *s1, struct smatch_state *s2)
{
	struct symbol *sym1, *sym2;
	char *str1, *str2;

	if (!s1->data || !s2->data)
		return &merged;

	if (s1->data == s2->data)
		return s1;

	str1 = expr_to_var_sym(s1->data, &sym1);
	str2 = expr_to_var_sym(s1->data, &sym2);
	if (!str1 || !str2)
		return &merged;

	if (sym1 == sym2 &&
	    strcmp(str1, str2) == 0)
		return s1;

	return &merged;
}

struct smatch_state *unmatched_buf_size_comparison(struct sm_state *sm)
{
	struct compare_data *data;
	sval_t sval;

	data = sm->state->data;
	if (!data)
		return &undefined;
	if (!get_implied_value(data->left, &sval) || sval.value != 0)
		return &undefined;

	return sm->state;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	sval_t sval;

	if (!sm->state->data ||
	    !get_implied_value(sm->state->data, &sval) ||
	    sval.value != 0)
		return &undefined;

	return sm->state;
}

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
	set_state(my_id, buf, NULL, alloc_expr_state(size));
}

static struct expression *ignored_assign;
static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	sval_t sval;

	if (expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return;
	if (!info->total_size)
		return;

	/* fixed size buffers are handled by smatch_buf_size.c */
	if (get_implied_value(info->total_size, &sval))
		return;

	record_size(expr->left, info->total_size, expr);
	ignored_assign = expr;
	// FIXME: info->nr_elems as well
}

static int get_param(int param, char **name, struct symbol **sym)
{
	struct symbol *arg;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		if (i == param) {
			if (!arg->ident)
				return false;
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

static void match_assign(struct expression *expr)
{
	struct smatch_state *state;
	char buf[64];
	char *name;

	if (expr->op != '=')
		return;
	if (expr == ignored_assign)
		return;
	if (__in_fake_assign || is_fake_var_assign(expr))
		return;

	name = expr_to_str(expr->right);
	if (!name)
		return;
	snprintf(buf, sizeof(buf), "$size %s", name);
	state = get_state(my_id, buf, NULL);
	if (!state || !state->data)
		return;

	record_size(expr->left, state->data, expr);
}

void smatch_buf_comparison2(int id)
{
	my_id = id;

	set_dynamic_states(my_id);
	add_unmatched_state_hook(my_id, unmatched_state);
	add_merge_hook(my_id, &merge_expr_states);
	add_allocation_hook(&match_allocation);
	select_caller_info_hook(set_param_compare, BYTE_COUNT);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}

