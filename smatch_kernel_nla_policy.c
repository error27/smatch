/*
 * Copyright (C) 2026 Dan Carpenter
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

static int my_id;

#define NLA_NUL_STRING_TYPE 10

struct nla_size_info {
	struct range_list *rl;
	bool found;
};

static const char *get_nl_data_attribute(struct expression *expr)
{
	struct expression *call;
	struct expression *attr;

	call = get_rightmost_call(expr);
	if (!call)
		return NULL;
	attr = get_argument_from_call_expr(call->args, 0);
	if (!attr)
		return NULL;
	attr = strip_expr(attr);
	if (attr && attr->type == EXPR_PREOP && attr->op == '*')
		attr = strip_expr(attr->unop);
	if (!attr || attr->type != EXPR_BINOP)
		return NULL;
	return pos_ident(attr->right->pos);
}

static int get_nla_size(void *_info, int argc, char **argv, char **azColName)
{
	struct nla_size_info *info = _info;
	sval_t min = { .type = &int_ctype };
	sval_t max = { .type = &int_ctype };
	char *end;
	long type, len;

	info->found = true;
	if (!argv[1])
		goto unknown;
	len = strtol(argv[1], &end, 10);
	if (*end || len < 0 || len >= INT_MAX)
		goto unknown;
	type = strtol(argv[0], &end, 10);
	if (*end)
		goto unknown;
	if (type == NLA_NUL_STRING_TYPE) {
		min.value = 1;
		max.value = len + 1;
	} else {
		min.value = 0;
		max.value = len;
	}
	add_range(&info->rl, min, max);
	return 0;
unknown:
	max.value = -1;
	add_range(&info->rl, max, max);
	return 0;
}

bool get_nl_data_size(struct expression *expr, struct range_list **rl)
{
	struct nla_size_info info = {};
	const char *name;

	*rl = NULL;
	name = get_nl_data_attribute(expr);
	if (!name)
		return false;
	run_sql(get_nla_size, &info,
		"select type, len from nla_policy where attribute = '%s';", name);
	*rl = info.rl;
	return info.found;
}

static int is_nla_nul_string(void *data, int argc, char **argv,
				     char **azColName)
{
	bool *result = data;
	char *end;
	long type;

	type = strtol(argv[0], &end, 10);
	if (!*end && type == NLA_NUL_STRING_TYPE)
		*result = true;
	return 0;
}

bool is_nl_data_nul_string(struct expression *expr)
{
	const char *name;
	bool result = false;

	name = get_nl_data_attribute(expr);
	if (!name)
		return false;
	run_sql(is_nla_nul_string, &result,
		"select type from nla_policy where attribute = '%s';", name);
	return result;
}

static const char *expr_value(struct expression *expr)
{
	sval_t sval;

	if (get_value(expr, &sval))
		return sval_to_str(sval);
	return expr_to_str(expr);
}

static const char *integer_value(struct expression *expr)
{
	sval_t sval;

	if (!get_value(expr, &sval))
		return "NULL";
	return sval_to_str(sval);
}

static void insert_policy(const char *attr, struct expression *initializer)
{
	struct expression *field;
	const char *type = "";
	const char *len = "NULL";
	const char *validation_type = "0";
	const char *min = "";
	const char *max = "";
	char value[256] = "";

	if (!initializer || initializer->type != EXPR_INITIALIZER)
		return;

	FOR_EACH_PTR(initializer->expr_list, field) {
		const char *name;

		if (field->type != EXPR_IDENTIFIER || !field->expr_ident ||
		    !field->ident_expression)
			continue;
		name = field->expr_ident->name;
		if (!strcmp(name, "type"))
			type = expr_value(field->ident_expression);
		else if (!strcmp(name, "len"))
			len = integer_value(field->ident_expression);
		else if (!strcmp(name, "validation_type"))
			validation_type = integer_value(field->ident_expression);
		else if (!strcmp(name, "min"))
			min = expr_value(field->ident_expression);
		else if (!strcmp(name, "max"))
			max = expr_value(field->ident_expression);
	} END_FOR_EACH_PTR(field);

	if (!type[0])
		return;
	if (min[0] && max[0])
		snprintf(value, sizeof(value), "%s-%s", min, max);
	sql_insert_or_ignore(nla_policy, "0x%llx, '%s', '%s', %s, %s, '%s'",
		get_base_file_id(), attr, type, len, validation_type, value);
}

static void match_global(struct symbol *sym)
{
	struct symbol *type;
	struct expression *entry;

	if (!sym->ident || !sym->initializer ||
	    sym->initializer->type != EXPR_INITIALIZER)
		return;
	type = get_base_type(sym);
	if (!type || type->type != SYM_ARRAY)
		return;
	type = get_base_type(type);
	if (!type || type->type != SYM_STRUCT || !type->ident ||
	    strcmp(type->ident->name, "nla_policy"))
		return;
	FOR_EACH_PTR(sym->initializer->expr_list, entry) {
		const char *attr;

		if (entry->type != EXPR_INDEX || !entry->idx_expression)
			continue;
		attr = pos_ident(entry->pos);
		if (!attr)
			attr = expr_value(entry);
		insert_policy(attr, entry->idx_expression);
	} END_FOR_EACH_PTR(entry);
}

void smatch_kernel_nla_policy(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;
	add_hook(&match_global, BASE_HOOK);
}
