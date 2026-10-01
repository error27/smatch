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
