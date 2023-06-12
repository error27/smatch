/*
 * Copyright (C) 2018 Oracle.
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

GLOBAL_STATE(unit_bit);
GLOBAL_STATE(unit_byte);
GLOBAL_STATE(unit_array_size);
GLOBAL_STATE(unit_long);
GLOBAL_STATE(unit_page);
GLOBAL_STATE(unit_msec);
GLOBAL_STATE(unit_ns);
GLOBAL_STATE(unit_jiffy);

struct type_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const char *value;
};

static struct type_info func_table[] = {
	{ "unit_msecs_to_jiffies_timeout", UNITS, -1, "$", "unit_jiffy" },
	{ "round_jiffies_up_relative", UNITS, -1, "$", "unit_jiffy" },
};

static struct smatch_state *str_to_units(const char *str)
{
	if (!str)
		return NULL;

	if (strcmp(str, "unit_bit") == 0)
		return &unit_bit;
	if (strcmp(str, "unit_byte") == 0)
		return &unit_byte;
	if (strcmp(str, "unit_page") == 0)
		return &unit_page;
	if (strcmp(str, "unit_msec") == 0)
		return &unit_msec;
	if (strcmp(str, "unit_jiffy") == 0)
		return &unit_jiffy;
	if (strcmp(str, "unit_long") == 0)
		return &unit_long;
	if (strcmp(str, "unit_array_size") == 0)
		return &unit_array_size;
	if (strcmp(str, "unknown") == 0)
		return NULL;

	return NULL;
}

static struct smatch_state *get_units_from_type(struct expression *expr);

static struct smatch_state *merge_units(struct smatch_state *s1, struct smatch_state *s2)
{
	if (s1 == &undefined)
		return s2;
	if (s2 == &undefined)
		return s1;
	return &merged;
}

static bool is_ignored_type(char *name)
{
	if (!name)
		return false;
	if (strcmp(name, "(union anonymous)->__val") == 0)
		return true;
	if (strncmp(name, "(struct fs_parse_result)", 24) == 0)
		return true;
	return false;
}

static bool ARRAY_SIZE_is_bytes(struct expression *expr)
{
	sval_t sval;

	expr = strip_expr(expr);
	if (!expr)
		return false;

	/*
	 * The kernel ARRAY_SIZE() macro is sizeof(array)/sizeof(array[0]) + 0
	 * where 0 does some fancy type checking.
	 *
	 */
	if (expr->type == EXPR_BINOP &&
	    expr->op == '+' &&
	    expr_is_zero(expr->right))
		return ARRAY_SIZE_is_bytes(expr->left);

	if (expr->type != EXPR_BINOP || expr->op != '/')
		return false;
	if (!get_value(expr->right, &sval) || sval.value != 1)
		return false;
	return true;
}

static void store_type_in_db(struct expression *expr, struct smatch_state *state)
{
	char *member;

	member = get_member_name(expr);
	if (!member)
		return;
	if (is_ignored_type(member))
		return;

	sql_insert_cache(type_info, "0x%llx, %d, '%s', '%s'", get_base_file_id(), UNITS, member, state->name);
}

static void set_units(struct expression *expr, struct smatch_state *state)
{
	if (!state)
		return;

	set_state_expr(my_id, expr, state);
	store_type_in_db(expr, state);
}

static bool is_PAGE_SHIFT(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (macro && strcmp(macro, "PAGE_SHIFT") == 0)
		return true;
	return false;
}

static bool is_PAGE_SIZE(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (macro && strcmp(macro, "PAGE_SIZE") == 0)
		return true;
	return false;
}

static bool is_BITS_PER_LONG(struct expression *expr)
{
	char *macro;

	macro = get_macro_name(expr->pos);
	if (macro && strcmp(macro, "BITS_PER_LONG") == 0)
		return true;
	return false;
}

static struct smatch_state *binop_helper(struct expression *left, int op, struct expression *right)
{
	struct smatch_state *left_state, *right_state;
	sval_t val;

	switch(op) {
	case '-':
		// subtracting pointers gives unit_byte
		/* fall through */
	case '+':
		left_state = get_units(left);
		right_state = get_units(right);
		if (left_state == &unit_array_size ||
		    right_state == &unit_array_size)
			return NULL;

		return left_state ? left_state : right_state;
	case '*':
		/* FIXME: A multiply is almost always bytes but it can be bits. */
		if (is_PAGE_SIZE(right))
			return &unit_byte;
		if (!get_implied_value(right, &val))
			return NULL;
		/* 4096 is almost always a unit_page -> bytes converstion */
		if (val.value == 4096)
			return &unit_byte;
		return NULL;
	case '/':
		if (is_BITS_PER_LONG(right))
			return &unit_long;
		if (is_PAGE_SIZE(right))
			return &unit_page;
		if (!get_implied_value(right, &val))
			return NULL;
		if (val.value == 4096)
			return &unit_page;
		return NULL;
	case SPECIAL_LEFTSHIFT:
		if (is_PAGE_SHIFT(right))
			return &unit_byte;
		return NULL;
	case SPECIAL_RIGHTSHIFT:
		if (is_PAGE_SHIFT(right))
			return &unit_page;
		return NULL;
	}
	return NULL;
}

static struct smatch_state *get_units_binop(struct expression *expr)
{
	return binop_helper(expr->left, expr->op, expr->right);
}

static struct smatch_state *get_units_call(struct expression *expr)
{
	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_CALL)
		return NULL;

	if (sym_name_is("unit_msecs_to_jiffies", expr->fn))
		return &unit_jiffy;
	if (sym_name_is("jiffies_to_unit_msecs", expr->fn))
		return &unit_msec;

	return NULL;
}

static int db_units(void *_units, int argc, char **argv, char **azColName)
{
	char **units = _units;

	if (*units) {
		if (strcmp(*units, argv[0]) == 0)
			return 0;
		free_string(*units);
		*units = alloc_string("unknown");
		return 0;

	}
	*units = alloc_string(argv[0]);
	return 0;
}

static struct smatch_state *get_units_from_type(struct expression *expr)
{
	char *member;
	char *units = NULL;
	struct smatch_state *ret = NULL;

	member = get_member_name(expr);
	if (!member)
		return NULL;
	if (strcmp(member, "(struct vm_area_struct)->vm_pgoff") == 0)
		return &unit_page;
	cache_sql(&db_units, &units, "select value from type_info where type = %d and key = '%s';",
		  UNITS, member);
	run_sql(&db_units, &units, "select value from type_info where type = %d and key = '%s';",
		UNITS, member);
	free_string(member);
	if (!units)
		return NULL;

	ret = str_to_units(units);

	free_string(units);

	return ret;
}

struct smatch_state *get_units(struct expression *expr)
{
	struct smatch_state *state;
	char *ident;

	expr = strip_expr(expr);
	if (!expr)
		return NULL;

	if (expr_is_zero(expr))
		return NULL;

	if (expr->type == EXPR_PTRSIZEOF ||
	    expr->type == EXPR_SIZEOF)
		return &unit_byte;

	ident = pos_ident(expr->pos);
	if (ident) {
		if (strcmp(ident, "sizeof") == 0 ||
		    strcmp(ident, "PAGE_SIZE") == 0)
			return &unit_byte;
		if (strcmp(ident, "jiffies") == 0)
			return &unit_jiffy;
		if (strcmp(ident, "BITS_PER_LONG") == 0)
			return &unit_bit;
		if (strcmp(ident, "BITS_PER_LONG_LONG") == 0)
			return &unit_bit;
		if (strcmp(ident, "ARRAY_SIZE") == 0) {
			if (ARRAY_SIZE_is_bytes(expr))
				return &unit_byte;
			return &unit_array_size;
		}
	}

	if (expr->type == EXPR_BINOP)
		return get_units_binop(expr);

	if (expr->type == EXPR_CALL)
		return get_units_call(expr);

	state = get_state_expr(my_id, expr);
	if (state == &merged || state == &undefined)
		return NULL;
	if (state)
		return state;

	return get_units_from_type(expr);
}

bool is_array_size_units(struct expression *expr)
{
	return get_units(expr) == &unit_array_size;
}

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	struct expression *right, *left;

	if (info->nr_elems && info->elem_size) {
		left = info->nr_elems;
		right = info->elem_size;
	} else if (info->total_size &&
		   info->total_size->type == EXPR_BINOP &&
		   info->total_size->op == '*') {
		left = strip_expr(info->total_size->left);
		right = strip_expr(info->total_size->right);
	} else {
		return;
	}

	if (get_units(left) == &unit_byte)
		set_units(right, &unit_array_size);
	if (get_units(right) == &unit_byte)
		set_units(left, &unit_array_size);
}

static void match_binop_set(struct expression *expr)
{
	struct smatch_state *left, *right;
	struct symbol *type;

	if (expr->op == SPECIAL_LEFTSHIFT && is_PAGE_SHIFT(expr->right)) {
		set_units(expr->left, &unit_page);
		return;
	}

	if (expr->op == SPECIAL_RIGHTSHIFT && is_PAGE_SHIFT(expr->right)) {
		set_units(expr->left, &unit_byte);
		return;
	}

	if (expr->op != '+' && expr->op != '-')
		return;

	type = get_type(expr->left);
	if (type && (type->type == SYM_PTR || type->type == SYM_ARRAY))
		return;

	left = get_units(expr->left);
	right = get_units(expr->right);

	if (left && !right)
		set_units(expr->right, left);
	if (right && !left)
		set_units(expr->left, right);
}

static void match_condition_set(struct expression *expr)
{
	struct smatch_state *left, *right;

	if (expr->type != EXPR_COMPARE)
		return;

	left = get_units(expr->left);
	right = get_units(expr->right);

	if (left && !right)
		set_units(expr->right, left);
	if (right && !left)
		set_units(expr->left, right);
}

static void match_assign(struct expression *expr)
{
	struct smatch_state *state = NULL;

	if (__in_fake_assign)
		return;

	switch(expr->op) {
	case '=':
		state = get_units(expr->right);
		break;
	case SPECIAL_SHR_ASSIGN:
	case SPECIAL_SHL_ASSIGN:
	case SPECIAL_DIV_ASSIGN:
	case SPECIAL_MUL_ASSIGN:
		state = binop_helper(expr->left, op_remove_assign(expr->op),
				     expr->right);
	}

	if (state)
		set_units(expr->left, state);
//	else
//		clear_units(expr->left);
}

static void set_implied_units(struct expression *call, struct expression *arg, char *key, char *value)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	state = str_to_units(value);
	if (!state)
		return;
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;
	set_state(my_id, name, sym, state);
free:
	free_string(name);
}

static void set_param_units(const char *name, struct symbol *sym, char *key, char *value)
{
	struct smatch_state *state;

	state = str_to_units(value);
	if (!state)
		return;
	set_state(my_id, sym->ident->name, sym, state);
}

static void set_param_units_from_table(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	const char *value = data;
	struct smatch_state *state;

	state = str_to_units(value);
	if (!state)
		return;
	set_state(my_id, name, sym, state);
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	struct smatch_state *state;
	char *fn_name;
	int param = -1;

	if (expr->fn->type != EXPR_SYMBOL)
		return;
	fn_name = expr_to_var(expr->fn);
	if (!fn_name)
		return;

	FOR_EACH_PTR(expr->args, arg) {
		param++;
		state = get_units(arg);
		if (!state)
			continue;
//		sql_insert_cache(return_implies, "0x%llx, '%s', 0, %d, %d, %d, '%s', '%s'",
//				 get_base_file_id(), fn_name, is_static(expr->fn), UNITS, param, "$", state->name);
		sql_insert_caller_info(expr, UNITS, param, "$", state->name);

	} END_FOR_EACH_PTR(arg);

	free_string(fn_name);
}

static void process_states(void)
{
	struct symbol *arg;
	struct smatch_state *state, *start_state;
	int param = -1;

	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		param++;
		state = get_state(my_id, arg->ident->name, arg);
		if (!state || state == &merged || state == &undefined)
			continue;
		start_state = get_state_stree(get_start_states(), my_id, arg->ident->name, arg);
		if (state == start_state)
			continue;
		sql_insert_cache(return_implies, "0x%llx, '%s', 0, %d, %d, %d, '%s', '%s'",
				 get_base_file_id(), get_function(), fn_static(), UNITS, param, "$", state->name);
	} END_FOR_EACH_PTR(arg);
}

char *get_unit_str(struct expression *expr)
{
	struct smatch_state *state;

	state = get_units(expr);
	if (!state)
		return NULL;
	return (char *)state->name;
}

void register_units(int id)
{
	struct type_info *info;
	int i;

	my_id = id;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];
		add_function_param_key_hook(info->name,
					    set_param_units_from_table,
					    info->param, info->key,
					    (void *)info->value);
	}

	add_merge_hook(my_id, &merge_units);

	add_hook(&match_binop_set, BINOP_HOOK);
	add_hook(&match_condition_set, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	all_return_states_hook(&process_states);

	select_return_implies_hook(UNITS, &set_implied_units);
	select_caller_info_hook(&set_param_units, UNITS);

	add_allocation_hook(&match_allocation);
}
