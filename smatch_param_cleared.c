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
 * This works together with smatch_clear_buffer.c.  This one is only for
 * tracking the information and smatch_clear_buffer.c changes SMATCH_EXTRA.
 *
 * This tracks functions like memset() which clear out a chunk of memory.
 * It fills in a gap that smatch_param_set.c can't handle.  It only handles
 * void pointers because smatch_param_set.c should handle the rest.  Oh.  And
 * also it handles arrays because Smatch sucks at handling arrays.
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(cleared);
STATE(zeroed);

struct func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const char *value;
	const sval_t *implies_start, *implies_end;
	param_key_hook *call_back;
};

static struct func_info func_table[] = {
	{ "memset", BUF_CLEARED, 0, "*$", "0"},
	{ "memzero", BUF_CLEARED, 0, "*$", "0" },
	{ "__memset", BUF_CLEARED, 0, "*$", "0"},
	{ "__memzero", BUF_CLEARED, 0, "*$", "0" },

	{ "memcpy", BUF_CLEARED, 0, "*$" },
	{ "memmove", BUF_CLEARED, 0, "*$" },
	{ "__memcpy", BUF_CLEARED, 0, "*$" },
	{ "__memmove", BUF_CLEARED, 0, "*$" },

	/* Should this be done some where else? */
	{ "strcpy", BUF_CLEARED, 0, "*$" },
	{ "strncpy", BUF_CLEARED, 0, "*$" },
	{ "sprintf", BUF_CLEARED, 0, "*$" },
	{ "snprintf", BUF_CLEARED, 0, "*$" },
};

static void db_param_cleared(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	arg = strip_expr(arg);
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	if (strcmp(value, "0") == 0)
		set_state(my_id, name, sym, &zeroed);
	else
		set_state(my_id, name, sym, &cleared);
free:
	free_string(name);
}

static void match_memcpy(const char *fn, struct expression *expr, void *arg)
{
	db_param_cleared(expr, PTR_INT(arg), (char *)"*$", (char *)"");
}

static void buf_cleared_db(struct expression *expr, const char *name, struct symbol *sym, const char *value)
{
	if (strcmp(value, "0") == 0)
		set_state(my_id, name, sym, &zeroed);
	else
		set_state(my_id, name, sym, &cleared);
}

static void buf_cleared(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct func_info *info = data;
	const char *value = "";

	if (info && info->value)
		value = info->value;

	buf_cleared_db(expr, name, sym, value);
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr)
{
	struct stree *stree;
	struct sm_state *sm;
	int param;
	const char *param_name;

	stree = __get_cur_stree();

	FOR_EACH_MY_SM(my_id, stree, sm) {
		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;

		param_name = get_param_name(sm);
		if (!param_name)
			continue;

		if (sm->state == &zeroed) {
			sql_insert_return_states(return_id, return_ranges,
						 BUF_CLEARED, param, param_name, "0");
		}

		if (sm->state == &cleared) {
			sql_insert_return_states(return_id, return_ranges,
						 BUF_CLEARED, param, param_name, "");
		}
	} END_FOR_EACH_SM(sm);
}

bool parent_was_PARAM_CLEAR(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	char buf[80];
	int len, i;

	if (!name)
		return 0;

	len = strlen(name);
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;

	for (i = len - 2; i >= 1; i--) {
		if (name[i] != '-' && name[i] != '.')
			continue;

		memcpy(buf, name, i);
		buf[i] = '\0';
		sm = get_sm_state(my_id, buf, sym);
		if (sm && sm->state == &cleared)
			return true;
		if (sm)
			return false;

		buf[0] = '&';
		memcpy(buf + 1, name, i);
		buf[i + 1] = '\0';
		sm = get_sm_state(my_id, buf, sym);
		if (sm && sm->state == &cleared)
			return true;
		if (sm)
			return false;
	}

	return false;
}

bool parent_was_PARAM_CLEAR_ZERO(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	char buf[80];
	int len, i;

	if (!name)
		return 0;

	len = strlen(name);
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;

	for (i = len - 2; i >= 1; i--) {
		if (name[i] != '-' && name[i] != '.')
			continue;

		memcpy(buf, name, i);
		buf[i] = '\0';
		sm = get_sm_state(my_id, buf, sym);
		if (sm && sm->state == &zeroed)
			return true;
		if (sm)
			return false;

		buf[0] = '&';
		memcpy(buf + 1, name, i);
		buf[i + 1] = '\0';
		sm = get_sm_state(my_id, buf, sym);
		if (sm && sm->state == &zeroed)
			return true;
		if (sm)
			return false;
	}

	return false;
}

static void register_clears_param(void)
{
	struct token *token;
	char name[256];
	const char *function;
	int param;

	if (option_project == PROJ_NONE)
		return;

	snprintf(name, 256, "%s.clears_argument", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		function = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		param = atoi(token->number);
		add_function_hook(function, &match_memcpy, INT_PTR(param));
		token = token->next;
	}
	clear_token_alloc();
}

#define USB_DIR_IN 0x80
static void match_usb_control_msg(const char *fn, struct expression *expr, void *_size_arg)
{
	struct expression *inout;
	sval_t sval;

	inout = get_argument_from_call_expr(expr->args, 3);

	if (get_value(inout, &sval) && !(sval.uvalue & USB_DIR_IN))
		return;

	db_param_cleared(expr, 6, (char *)"*$", (char *)"");
}

static void match_assign(struct expression *expr)
{
	struct symbol *type;

	/*
	 * If we have struct foo x, y; and we say that x = y; then it
	 * initializes the struct holes.  So we record that here.
	 */
	type = get_type(expr->left);
	if (!type || type->type != SYM_STRUCT)
		return;

	set_state_expr(my_id, expr->left, &cleared);
}

static void match_array_assign(struct expression *expr)
{
	struct expression *array_expr;

	if (!is_array(expr->left))
		return;

	array_expr = get_array_base(expr->left);
	set_state_expr(my_id, array_expr, &cleared);
}

static void load_func_table(struct func_info *table, int size)
{
	struct func_info *info;
	param_key_hook *cb;
	int i;

	for (i = 0; i < size; i++) {
		info = &table[i];

		if (info->call_back)
			cb = info->call_back;
		else
			cb = buf_cleared;

		if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start, *info->implies_end,
					cb, info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name, cb,
					info->param, info->key, info);
		}
	}
}

void register_param_cleared(int id)
{
	my_id = id;


	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_array_assign, ASSIGNMENT_HOOK);

	register_clears_param();

	select_return_states_hook(BUF_CLEARED, &db_param_cleared);
	add_split_return_callback(&print_return_value_param);

	if (option_project == PROJ_KERNEL)
		add_function_hook("usb_control_msg", &match_usb_control_msg, NULL);

	load_func_table(func_table, ARRAY_SIZE(func_table));
	if (option_project == PROJ_KERNEL)
		load_func_table(kernel_func_table, ARRAY_SIZE(kernel_func_table));
}

