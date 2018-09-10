/*
 * Copyright (C) 2018 Oracle.  All rights reserved.
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

static int get_vals(void *_db_vals, int argc, char **argv, char **azColName)
{
	char **db_vals = _db_vals;

	*db_vals = alloc_string(argv[0]);
	return 0;
}

int get_array_rl(struct expression *expr, struct range_list **rl)
{
	struct expression *array;
	struct symbol *type;
	char buf[128];
	char *rl_str = NULL;
	char *name;

	array = get_array_base(expr);
	if (!array || array->type != EXPR_SYMBOL)
		return 0;
	if (!(array->symbol->ctype.modifiers & MOD_TOPLEVEL) ||
	    !(array->symbol->ctype.modifiers & MOD_STATIC))
		return 0;

	type = get_type(expr);
	if (!type || type->type != SYM_BASETYPE)
		return 0;

	name = expr_to_str(array);
	snprintf(buf, sizeof(buf), "%s[]", name);
	free_string(name);

	run_sql(&get_vals, &rl_str,
		"select value from sink_info where file = '%s' and static = 1 and sink_name = '%s' and type = %d;",
		get_filename(), buf, DATA_VALUE);
	if (!rl_str)
		return 0;

	str_to_rl(type, rl_str, rl);
	free_string(rl_str);
	return 1;
}

static struct range_list *get_saved_rl(struct symbol *type, char *name)
{
	struct range_list *rl;
	char *str = NULL;

	cache_sql(&get_vals, &str, "select value from sink_info where file = '%s' and static = 1 and sink_name = '%s' and type = %d;",
		  get_filename(), name, DATA_VALUE);
	if (!str)
		return NULL;

	str_to_rl(type, str, &rl);
	free_string(str);

	return rl;
}

static void update_cache(char *name, struct range_list *rl)
{
	cache_sql(NULL, NULL, "delete from sink_info where file = '%s' and static = 1 and sink_name = '%s' and type = %d;",
		  get_filename(), name, DATA_VALUE);
	cache_sql(NULL, NULL, "insert into sink_info values ('%s', 1, '%s', %d, '', '%s');",
		  get_filename(), name, DATA_VALUE, show_rl(rl));
}

static void match_assign(struct expression *expr)
{
	struct expression *left, *array;
	struct symbol *type;
	struct range_list *orig_rl, *rl;
	char *name;
	char buf[128];

	left = strip_expr(expr->left);
	if (!is_array(left))
		return;
	array = get_array_base(left);
	if (!array || array->type != EXPR_SYMBOL)
		return;
	if (!(array->symbol->ctype.modifiers & MOD_TOPLEVEL) ||
	    !(array->symbol->ctype.modifiers & MOD_STATIC))
		return;
	type = get_type(array);
	if (!type || type->type != SYM_ARRAY)
		return;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_BASETYPE)
		return;

	name = expr_to_str(array);
	snprintf(buf, sizeof(buf), "%s[]", name);
	free_string(name);

	if (expr->op != '=') {
		rl = alloc_whole_rl(type);
	} else {
		get_absolute_rl(expr->right, &rl);
		rl = cast_rl(type, rl);
		orig_rl = get_saved_rl(type, buf);
		rl = rl_union(orig_rl, rl);
	}

	update_cache(buf, rl);
}

void register_array_values(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_assign, GLOBAL_ASSIGNMENT_HOOK);
}
