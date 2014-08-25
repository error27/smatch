/*
 * Copyright (C) 2010 Dan Carpenter.
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

#include <stdlib.h>
#include <errno.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include "smatch_function_hashtable.h"

#define UNKNOWN_SIZE (-1)

static int my_size_id;

struct limiter {
	int buf_arg;
	int limit_arg;
};
static struct limiter b0_l2 = {0, 2};

static DEFINE_HASHTABLE_INSERT(insert_func, char, int);
static DEFINE_HASHTABLE_SEARCH(search_func, char, int);
static struct hashtable *allocation_funcs;

static char *get_fn_name(struct expression *expr)
{
	if (expr->type != EXPR_CALL)
		return NULL;
	if (expr->fn->type != EXPR_SYMBOL)
		return NULL;
	return expr_to_var(expr->fn);
}

static int is_allocation_function(struct expression *expr)
{
	char *func;
	int ret = 0;

	func = get_fn_name(expr);
	if (!func)
		return 0;
	if (search_func(allocation_funcs, func))
		ret = 1;
	free_string(func);
	return ret;
}

static void add_allocation_function(const char *func, void *call_back, int param)
{
	insert_func(allocation_funcs, (char *)func, (int *)1);
	add_function_assign_hook(func, call_back, INT_PTR(param));
}

static int estate_to_size(struct smatch_state *state)
{
	sval_t sval;

	if (!state || !estate_rl(state))
		return 0;
	sval = estate_max(state);
	return sval.value;
}

static struct smatch_state *size_to_estate(int size)
{
	sval_t sval;

	sval.type = &int_ctype;
	sval.value = size;

	return alloc_estate_sval(sval);
}

static struct range_list *size_to_rl(int size)
{
	sval_t sval;

	sval.type = &int_ctype;
	sval.value = size;

	return alloc_rl(sval, sval);
}

static struct smatch_state *unmatched_size_state(struct sm_state *sm)
{
	return size_to_estate(UNKNOWN_SIZE);
}

static void set_size_undefined(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(sm->owner, sm->name, sm->sym, size_to_estate(UNKNOWN_SIZE));
}

static struct smatch_state *merge_size_func(struct smatch_state *s1, struct smatch_state *s2)
{
	return merge_estates(s1, s2);
}

void set_param_buf_size(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	char fullname[256];

	if (strncmp(key, "$$", 2) != 0)
		return;

	snprintf(fullname, 256, "%s%s", name, key + 2);

	str_to_rl(&int_ctype, value, &rl);
	if (!rl || is_whole_rl(rl))
		return;
	state = alloc_estate_rl(rl);
	set_state(my_size_id, fullname, sym, state);
}

static int bytes_per_element(struct expression *expr)
{
	struct symbol *type;

	if (expr->type == EXPR_STRING)
		return 1;
	type = get_type(expr);
	if (!type)
		return 0;

	if (type->type != SYM_PTR && type->type != SYM_ARRAY)
		return 0;

	type = get_base_type(type);
	return type_bytes(type);
}

static int bytes_to_elements(struct expression *expr, int bytes)
{
	int bpe;

	bpe = bytes_per_element(expr);
	if (bpe == 0)
		return 0;
	return bytes / bpe;
}

static int elements_to_bytes(struct expression *expr, int elements)
{
	int bpe;

	bpe = bytes_per_element(expr);
	return elements * bpe;
}

static int get_initializer_size(struct expression *expr)
{
	switch (expr->type) {
	case EXPR_STRING:
		return expr->string->length;
	case EXPR_INITIALIZER: {
		struct expression *tmp;
		int i = 0;

		FOR_EACH_PTR(expr->expr_list, tmp) {
			if (tmp->type == EXPR_INDEX) {
				if (tmp->idx_to >= i)
					i = tmp->idx_to;
				else
					continue;
			}

			i++;
		} END_FOR_EACH_PTR(tmp);
		return i;
	}
	case EXPR_SYMBOL:
		return get_array_size(expr);
	}
	return 0;
}

static struct range_list *db_size_rl;
static int db_size_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct range_list *tmp = NULL;

	if (!db_size_rl) {
		str_to_rl(&int_ctype, argv[0], &db_size_rl);
	} else {
		str_to_rl(&int_ctype, argv[0], &tmp);
		db_size_rl = rl_union(db_size_rl, tmp);
	}
	return 0;
}

static struct range_list *size_from_db(struct expression *expr)
{
	int this_file_only = 0;
	char *name;

	name = get_member_name(expr);
	if (!name && is_static(expr)) {
		name = expr_to_var(expr);
		this_file_only = 1;
	}
	if (!name)
		return 0;

	if (this_file_only) {
		db_size_rl = NULL;
		run_sql(db_size_callback, "select size from function_type_size where type = '%s' and file = '%s';",
				name, get_filename());
		if (db_size_rl)
			return db_size_rl;
		return 0;
	}

	db_size_rl = NULL;
	run_sql(db_size_callback, "select size from type_size where type = '%s';",
			name);
	return db_size_rl;
}

static void db_returns_buf_size(struct expression *expr, int param, char *unused, char *math)
{
	struct expression *call;
	sval_t sval;

	if (expr->type != EXPR_ASSIGNMENT)
		return;
	call = strip_expr(expr->right);

	if (!parse_call_math(call, math, &sval))
		return;
	set_state_expr(my_size_id, expr->left, size_to_estate(sval.value));
}

int get_real_array_size(struct expression *expr)
{
	struct symbol *type;
	sval_t sval;

	if (expr->type == EXPR_BINOP) /* array elements foo[5] */
		return 0;

	type = get_type(expr);
	if (!type)
		return 0;
	if (!type || type->type != SYM_ARRAY)
		return 0;

	if (!get_implied_value(type->array_size, &sval))
		return 0;

	/* People put one element arrays on the end of structs */
	if (sval.value == 1)
		return 0;

	return sval.value;
}

static int get_size_from_initializer(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol || !expr->symbol->initializer)
		return 0;
	if (expr->symbol->initializer == expr) /* int a = a; */
		return 0;
	return get_initializer_size(expr->symbol->initializer);
}

static struct range_list *get_stored_size_bytes(struct expression *expr)
{
	struct smatch_state *state;

	state = get_state_expr(my_size_id, expr);
	if (!state)
		return NULL;
	return estate_rl(state);
}

static int get_bytes_from_address(struct expression *expr)
{
	struct symbol *type;
	int ret;

	if (!option_spammy)
		return 0;
	if (expr->type != EXPR_PREOP || expr->op != '&')
		return 0;
	type = get_type(expr);
	if (!type)
		return 0;

	if (type->type == SYM_PTR)
		type = get_base_type(type);

	ret = type_bytes(type);
	if (ret == 1)
		return 0;  /* ignore char pointers */

	return ret;
}

static struct expression *remove_addr_fluff(struct expression *expr)
{
	struct expression *tmp;
	sval_t sval;

	expr = strip_expr(expr);

	/* remove '&' and '*' operations that cancel */
	while (expr && expr->type == EXPR_PREOP && expr->op == '&') {
		tmp = strip_expr(expr->unop);
		if (tmp->type != EXPR_PREOP)
			break;
		if (tmp->op != '*')
			break;
		expr = strip_expr(tmp->unop);
	}

	if (!expr)
		return NULL;

	/* "foo + 0" is just "foo" */
	if (expr->type == EXPR_BINOP && expr->op == '+' &&
	    get_value(expr->right, &sval) && sval.value == 0)
		return expr->left;

	return expr;
}

static int is_last_member_of_struct(struct symbol *sym, struct ident *member)
{
	struct symbol *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR_REVERSE(sym->symbol_list, tmp) {
		if (i++ || !tmp->ident)
			return 0;
		if (tmp->ident == member)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);

	return 0;
}

static int get_stored_size_end_struct_bytes(struct expression *expr)
{
	struct symbol *type;
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	sval_t sval;

	if (expr->type == EXPR_BINOP) /* array elements foo[5] */
		return 0;

	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return 0;

	if (!get_implied_value(type->array_size, &sval))
		return 0;

	if (sval.value != 0 && sval.value != 1)
		return 0;

	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!sym || !sym->ident)
		return 0;
	if (!type_bytes(sym))
		return 0;

	if (sym->type != SYM_NODE)
		return 0;

	state = get_state(my_size_id, sym->ident->name, sym);
	if (!estate_to_size(state))
		return 0;

	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_PTR)
		return 0;
	sym = get_real_base_type(sym);
	if (!sym || sym->type != SYM_STRUCT)
		return 0;
	if (!is_last_member_of_struct(sym, expr->member))
		return 0;

	return estate_to_size(state) - type_bytes(sym) + type_bytes(type);
}

static struct range_list *alloc_int_rl(int value)
{
	sval_t sval = {
		.type = &int_ctype,
		{.value = value},
	};

	return alloc_rl(sval, sval);
}

struct range_list *get_array_size_bytes_rl(struct expression *expr)
{
	int declared_size = 0;
	struct range_list *ret = NULL;
	int size;

	expr = remove_addr_fluff(expr);
	if (!expr)
		return NULL;

	/* "BAR" */
	if (expr->type == EXPR_STRING)
		return alloc_int_rl(expr->string->length);

	/* buf[4] */
	size = get_real_array_size(expr);
	if (size)
		declared_size = elements_to_bytes(expr, size);

	/* buf = malloc(1024); */
	ret = get_stored_size_bytes(expr);
	if (ret) {
		if (declared_size)
			return rl_union(ret, alloc_int_rl(size));
		return ret;
	}
	if (declared_size)
		return alloc_int_rl(declared_size);

	size = get_stored_size_end_struct_bytes(expr);
	if (size)
		return alloc_int_rl(size);

	/* char *foo = "BAR" */
	size = get_size_from_initializer(expr);
	if (size)
		return alloc_int_rl(elements_to_bytes(expr, size));

	size = get_bytes_from_address(expr);
	if (size)
		return alloc_int_rl(size);

	/* if (strlen(foo) > 4) */
	size = get_size_from_strlen(expr);
	if (size)
		return alloc_int_rl(size);

	ret = size_from_db(expr);
	if (ret)
		return ret;

	return NULL;
}

int get_array_size_bytes(struct expression *expr)
{
	struct range_list *rl;
	sval_t sval;

	rl = get_array_size_bytes_rl(expr);
	if (!rl_to_sval(rl, &sval))
		return 0;
	if (sval.uvalue >= INT_MAX)
		return 0;
	return sval.value;
}

int get_array_size_bytes_max(struct expression *expr)
{
	struct range_list *rl;
	sval_t bytes;

	rl = get_array_size_bytes_rl(expr);
	if (!rl)
		return 0;
	bytes = rl_min(rl);
	if (bytes.value < 0)
		return 0;
	bytes = rl_max(rl);
	if (bytes.uvalue >= INT_MAX)
		return 0;
	return bytes.value;
}

int get_array_size_bytes_min(struct expression *expr)
{
	struct range_list *rl;
	struct data_range *range;

	rl = get_array_size_bytes_rl(expr);
	if (!rl)
		return 0;

	FOR_EACH_PTR(rl, range) {
		if (range->min.value <= 0)
			return 0;
		if (range->max.value <= 0)
			return 0;
		if (range->min.uvalue >= INT_MAX)
			return 0;
		return range->min.value;
	} END_FOR_EACH_PTR(range);

	return 0;
}

int get_array_size(struct expression *expr)
{
	return bytes_to_elements(expr, get_array_size_bytes_max(expr));
}

static void match_strlen_condition(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct expression *str = NULL;
	int strlen_left = 0;
	int strlen_right = 0;
	sval_t sval;
	struct smatch_state *true_state = NULL;
	struct smatch_state *false_state = NULL;

	if (expr->type != EXPR_COMPARE)
		return;
	left = strip_expr(expr->left);
	right = strip_expr(expr->right);

	if (left->type == EXPR_CALL && sym_name_is("strlen", left->fn)) {
		str = get_argument_from_call_expr(left->args, 0);
		strlen_left = 1;
	}
	if (right->type == EXPR_CALL && sym_name_is("strlen", right->fn)) {
		str = get_argument_from_call_expr(right->args, 0);
		strlen_right = 1;
	}

	if (!strlen_left && !strlen_right)
		return;
	if (strlen_left && strlen_right)
		return;

	if (strlen_left) {
		if (!get_value(right, &sval))
			return;
	}
	if (strlen_right) {
		if (!get_value(left, &sval))
			return;
	}

	/* FIXME:  why are we using my_size_id here instead of my_strlen_id */

	if (expr->op == SPECIAL_EQUAL) {
		set_true_false_states_expr(my_size_id, str, size_to_estate(sval.value + 1), NULL);
		return;
	}
	if (expr->op == SPECIAL_NOTEQUAL) {
		set_true_false_states_expr(my_size_id, str, NULL, size_to_estate(sval.value + 1));
		return;
	}

	switch (expr->op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (strlen_left)
			true_state = size_to_estate(sval.value);
		else
			false_state = size_to_estate(sval.value + 1);
		break;
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LTE:
		if (strlen_left)
			true_state = size_to_estate(sval.value + 1);
		else
			false_state = size_to_estate(sval.value);
		break;
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		if (strlen_left)
			false_state = size_to_estate(sval.value);
		else
			true_state = size_to_estate(sval.value + 1);
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (strlen_left)
			false_state = size_to_estate(sval.value + 1);
		else
			true_state = size_to_estate(sval.value);
		break;
	}
	set_true_false_states_expr(my_size_id, str, true_state, false_state);
}

static struct expression *strip_ampersands(struct expression *expr)
{
	struct symbol *type;

	if (expr->type != EXPR_PREOP)
		return expr;
	if (expr->op != '&')
		return expr;
	type = get_type(expr->unop);
	if (!type || type->type != SYM_ARRAY)
		return expr;
	return expr->unop;
}

static void info_record_alloction(struct expression *buffer, struct range_list *rl)
{
	char *name;

	if (!option_info)
		return;

	name = get_member_name(buffer);
	if (!name && is_static(buffer))
		name = expr_to_var(buffer);
	if (!name)
		return;
	if (rl && !is_whole_rl(rl))
		sql_insert_function_type_size(name, show_rl(rl));
	else
		sql_insert_function_type_size(name, "(-1)");

	free_string(name);
}

static void store_alloc(struct expression *expr, struct range_list *rl)
{
	rl = clone_rl(rl); // FIXME!!!
	info_record_alloction(expr, rl);
	set_state_expr(my_size_id, expr, alloc_estate_rl(rl));
}

static void match_array_assignment(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct range_list *rl;
	sval_t sval;

	if (expr->op != '=')
		return;
	left = strip_expr(expr->left);
	right = strip_expr(expr->right);
	right = strip_ampersands(right);

	if (is_allocation_function(right))
		return;

	if (get_implied_value(right, &sval) && sval.value == 0) {
		rl = alloc_int_rl(0);
		goto store;
	}

	rl = get_array_size_bytes_rl(right);

store:
	store_alloc(left, rl);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *right;
	struct expression *arg;
	struct range_list *rl;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, size_arg);
	get_absolute_rl(arg, &rl);
	rl = cast_rl(&int_ctype, rl);
	store_alloc(expr->left, rl);
}

static void match_calloc(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *arg;
	sval_t elements;
	sval_t size;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, 0);
	if (!get_implied_value(arg, &elements))
		return; // FIXME!!!
	arg = get_argument_from_call_expr(right->args, 1);
	if (get_implied_value(arg, &size))
		store_alloc(expr->left, size_to_rl(elements.value * size.value));
	else
		store_alloc(expr->left, size_to_rl(-1));
}

static void match_limited(const char *fn, struct expression *expr, void *_limiter)
{
	struct limiter *limiter = (struct limiter *)_limiter;
	struct expression *dest;
	struct expression *size_expr;
	sval_t size;

	dest = get_argument_from_call_expr(expr->args, limiter->buf_arg);
	size_expr = get_argument_from_call_expr(expr->args, limiter->limit_arg);
	if (!get_implied_max(size_expr, &size))
		return;
	set_state_expr(my_size_id, dest, size_to_estate(size.value));
}

static void match_strcpy(const char *fn, struct expression *expr, void *unused)
{
	struct expression fake_assign;

	fake_assign.op = '=';
	fake_assign.left = get_argument_from_call_expr(expr->args, 0);
	fake_assign.right = get_argument_from_call_expr(expr->args, 1);
	match_array_assignment(&fake_assign);
}

static void match_strndup(const char *fn, struct expression *expr, void *unused)
{
	struct expression *fn_expr;
	struct expression *size_expr;
	sval_t size;

	fn_expr = strip_expr(expr->right);
	size_expr = get_argument_from_call_expr(fn_expr->args, 1);
	if (get_implied_max(size_expr, &size)) {
		size.value++;
		store_alloc(expr->left, size_to_rl(size.value));
	} else {
		store_alloc(expr->left, size_to_rl(-1));
	}

}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	struct range_list *rl;
	int i;

	i = 0;
	FOR_EACH_PTR(expr->args, arg) {
		rl = get_array_size_bytes_rl(arg);
		if (rl && !is_whole_rl(rl))
			sql_insert_caller_info(expr, BUF_SIZE, i, "$$", show_rl(rl));
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct smatch_state *state)
{
	if (state == &merged)
		return;
	sql_insert_caller_info(call, BUF_SIZE, param, printed_name, state->name);
}

void register_buf_size(int id)
{
	my_size_id = id;

	add_unmatched_state_hook(my_size_id, &unmatched_size_state);

	select_caller_info_hook(set_param_buf_size, BUF_SIZE);
	select_return_states_hook(BUF_SIZE, &db_returns_buf_size);

	allocation_funcs = create_function_hashtable(100);
	add_allocation_function("malloc", &match_alloc, 0);
	add_allocation_function("calloc", &match_calloc, 0);
	add_allocation_function("memdup", &match_alloc, 1);
	if (option_project == PROJ_KERNEL) {
		add_allocation_function("kmalloc", &match_alloc, 0);
		add_allocation_function("kzalloc", &match_alloc, 0);
		add_allocation_function("vmalloc", &match_alloc, 0);
		add_allocation_function("__vmalloc", &match_alloc, 0);
		add_allocation_function("kcalloc", &match_calloc, 0);
		add_allocation_function("kmalloc_array", &match_calloc, 0);
		add_allocation_function("drm_malloc_ab", &match_calloc, 0);
		add_allocation_function("drm_calloc_large", &match_calloc, 0);
		add_allocation_function("sock_kmalloc", &match_alloc, 1);
		add_allocation_function("kmemdup", &match_alloc, 1);
		add_allocation_function("kmemdup_user", &match_alloc, 1);
		add_allocation_function("dma_alloc_attrs", &match_alloc, 1);
		add_allocation_function("pci_alloc_consistent", &match_alloc, 1);
		add_allocation_function("pci_alloc_coherent", &match_alloc, 1);
		add_allocation_function("devm_kmalloc", &match_alloc, 1);
		add_allocation_function("devm_kzalloc", &match_alloc, 1);
	}
	add_hook(&match_strlen_condition, CONDITION_HOOK);

	add_allocation_function("strndup", match_strndup, 0);
	if (option_project == PROJ_KERNEL)
		add_allocation_function("kstrndup", match_strndup, 0);

	add_modification_hook(my_size_id, &set_size_undefined);

	add_merge_hook(my_size_id, &merge_size_func);
}

void register_buf_size_late(int id)
{
	/* has to happen after match_alloc() */
	add_hook(&match_array_assignment, ASSIGNMENT_HOOK);

	add_function_hook("strlcpy", &match_limited, &b0_l2);
	add_function_hook("strlcat", &match_limited, &b0_l2);
	add_function_hook("memscan", &match_limited, &b0_l2);

	add_function_hook("strcpy", &match_strcpy, NULL);

	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_size_id, struct_member_callback);
}
