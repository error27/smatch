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

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	if (param < 0)
		return;

	if (sm->state != &zeroed &&
	    sm->state != &cleared)
		return;

	sql_insert_return_states(return_id, return_ranges, BUF_CLEARED, param,
			printed_name, (sm->state == &zeroed) ? "0" : "");
}

static bool is_parent(struct sm_state *sm, const char *name, struct symbol *sym, int name_len)
{
	const char *sm_name, *var_name;
	int shared = 0;
	int i;

	if (sm->sym != sym)
		return false;

	/* I think sm->name always starts with a '*' now */
	if (sm->name[0] != '*')
		return false;
	sm_name = &sm->name[1];
	var_name = name;
	if (var_name[0] == '*')
		var_name++;

	for (i = 0; i < name_len; i++) {
		if (!sm_name[i])
			break;
		if (sm_name[i] == var_name[i])
			shared++;
		else
			break;
	}

	if (sm_name[shared] != '\0')
		return false;

	if (var_name[shared] == '.' ||
	    var_name[shared] == '-' ||
	    var_name[shared] == '\0')
		return true;

	return false;
}

enum clear_zero {
	CLEAR,
	ZERO,
	ANY,
};

static bool parent_was_clear(const char *name, struct symbol *sym, enum clear_zero zero)
{
	struct sm_state *sm;
	char buf[250];
	int len, i;

	if (!name || !sym)
		return false;

	len = strlen(name);
	if (len >= sizeof(buf)) {
		/*
		 * Haha.  If your variable is over 250 chars I want nothing to
		 * to with it.
		 */
		return true;
	}

	for (i = len - 1; i > 0; i--) {
		if (name[i] == '.' || name[i] == '-')
			break;
	}
	if (i == 0)
		return false;
	memcpy(buf, name, i);
	buf[i] = '\0';

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (!is_parent(sm, name, sym, len))
			continue;
		if (zero == ZERO && sm->state == &zeroed)
			return true;
		if (zero == CLEAR && sm->state == &cleared)
			return true;
		if (zero == ANY)
			return true;
		return false;
	} END_FOR_EACH_SM(sm);

	return false;
}

bool parent_was_PARAM_CLEAR(const char *name, struct symbol *sym)
{
	return parent_was_clear(name, sym, CLEAR);
}

bool parent_was_PARAM_CLEAR_ZERO(const char *name, struct symbol *sym)
{
	return parent_was_clear(name, sym, ZERO);
}

static bool already_printed(struct symbol *arg)
{
	if (!arg || !arg->ident || !arg->ident->name)
		return false;

	return parent_was_clear(arg->ident->name, arg, ANY);
}

static bool sym_is_void_pointer(struct symbol *sym)
{
	struct symbol *type;

	type = get_real_base_type(sym);
	if (!type || type->type != SYM_PTR)
		return false;
	type = get_real_base_type(type);

	return type == &void_ctype;
}

static bool sym_is_union_pointer(struct symbol *sym)
{
	struct symbol *type;

	type = get_real_base_type(sym);
	if (!type || type->type != SYM_PTR)
		return false;
	while (type && type->type == SYM_PTR)
		type = get_real_base_type(type);
	if (!type || type->type != SYM_UNION)
		return false;

	return true;
}

static bool ambiguous_members_set(struct symbol *arg)
{
	struct sm_state *sm;
	static int param_set_id;
	int cnt = 0;

	if (!param_set_id)
		param_set_id = id_from_name("register_param_set");

	if (!arg || !arg->ident || !arg->ident->name)
		return false;

	if (!sym_is_void_pointer(arg) &&
	    !sym_is_union_pointer(arg))
		return false;

	FOR_EACH_MY_SM(param_set_id, __get_cur_stree(), sm) {
		if (sm->sym == arg)
			cnt++;
	} END_FOR_EACH_SM(sm);

	return cnt > 100;
}

static bool all_struct_members_set(struct symbol *arg)
{
	struct symbol *type, *tmp;
	char buf[80];

	if (!arg || !arg->ident || !arg->ident->name)
		return false;

	type = get_real_base_type(arg);
	if (!type || type->type != SYM_PTR)
		return false;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return false;

	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (!tmp->ident)
			return false;
		snprintf(buf, sizeof(buf), "%s->%s", arg->ident->name, tmp->ident->name);
		if (!param_was_set_var_sym(buf, arg))
			return false;
	} END_FOR_EACH_PTR(tmp);

	return true;
}

void __promote_sets_to_clears(int return_id, char *return_ranges, struct expression *expr)
{
	struct symbol *arg;
	char buf[256];
	int i;

	/*
	 * This is called after BUF_CLEARED variables have been recorded but
	 * before PARAM_SET.  If all the struct members have been set then
	 * promote it to BUF_CLEARED.
	 */

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		i++;

		if (!arg->ident)
			continue;
		if (already_printed(arg))
			continue;
		if (ambiguous_members_set(arg) ||
		    all_struct_members_set(arg)) {

			snprintf(buf, sizeof(buf), "*%s", arg->ident->name);
			set_state(my_id, buf, arg, &cleared);
			sql_insert_return_states(return_id, return_ranges,
					BUF_CLEARED, i, "*$", "");
		}
	} END_FOR_EACH_PTR(arg);
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

int param_add_set_counter[12];
static void db_counter_reset(struct expression *expr)
{
	memset(param_add_set_counter, 0, sizeof(param_add_set_counter));
}

static void db_counter_inc(struct expression *expr, int param, char *key, char *value)
{
	if (param < 0 || param >= ARRAY_SIZE(param_add_set_counter))
		return;
	param_add_set_counter[param]++;
}

static void promote_void_param_sets(struct expression *expr)
{
	struct expression *arg, *deref;
	struct symbol *type;
	int i;

	if (expr->type != EXPR_CALL) {
		sm_msg("unexpected!");
		return;
	}

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;

		if (param_add_set_counter[i] < 100)
			continue;

		type = get_arg_type(expr->fn, i);
		if (!type || type->type != SYM_PTR)
			continue;
		type = get_real_base_type(type);
		if (type != &void_ctype)
			continue;

		deref = deref_expression(arg);
		set_state_expr(my_id, deref, &cleared);
	} END_FOR_EACH_PTR(arg);
}

void register_param_cleared(int id)
{
	my_id = id;

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_array_assign, ASSIGNMENT_HOOK);

	register_clears_param();

	select_return_states_hook(BUF_CLEARED, &db_param_cleared);
	add_return_info_callback(my_id, return_info_callback);

	if (option_project == PROJ_KERNEL)
		add_function_hook("usb_control_msg", &match_usb_control_msg, NULL);

	load_func_table(func_table, ARRAY_SIZE(func_table));

	add_hook(&db_counter_reset, CALL_HOOK_AFTER_INLINE);
	select_return_states_hook(PARAM_SET, &db_counter_inc);
	select_return_states_hook(PARAM_ADD, &db_counter_inc);
	add_hook(&promote_void_param_sets, FUNCTION_CALL_HOOK_AFTER_DB);
}

