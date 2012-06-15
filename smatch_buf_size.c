/*
 * smatch/smatch_buf_size.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

/*
 * This check has two smatch IDs.
 * my_size_id - used to store the size of arrays.
 * my_strlen_id - track the strlen() of buffers.
 */

static int my_size_id;
static int my_strlen_id;

struct limiter {
	int buf_arg;
	int limit_arg;
};
static struct limiter b0_l2 = {0, 2};

static _Bool params_set[32];

static void set_undefined(struct sm_state *sm)
{
	if (sm->state != &undefined)
		set_state(sm->owner, sm->name, sm->sym, &undefined);
}

void set_param_buf_size(const char *name, struct symbol *sym, char *key, char *value)
{
	char fullname[256];
	unsigned int size;

	if (strncmp(key, "$$", 2))
		return;

	snprintf(fullname, 256, "%s%s", name, key + 2);

	errno = 0;
	size = strtoul(value, NULL, 10);
	if (errno)
		return;

	set_state(my_size_id, fullname, sym, alloc_state_num(size));
}

static int bytes_per_element(struct expression *expr)
{
	struct symbol *type;
	int bpe;

	if (expr->type == EXPR_STRING)
		return 1;
	type = get_type(expr);
	if (!type)
		return 0;

	if (type->type != SYM_PTR && type->type != SYM_ARRAY)
		return 0;

	type = get_base_type(type);
	bpe = bits_to_bytes(type->bit_size);

	if (bpe == -1) /* void pointer */
		bpe = 1;

	return bpe;
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
		int max = 0;

		FOR_EACH_PTR(expr->expr_list, tmp) {
			if (tmp->type == EXPR_INDEX && tmp->idx_to > max)
				max = tmp->idx_to;
			i++;
		} END_FOR_EACH_PTR(tmp);
		if (max)
			return max + 1;
		return i;
	}
	case EXPR_SYMBOL:
		return get_array_size(expr);
	}
	return 0;
}

static int db_size;
static int db_size_callback(void *unused, int argc, char **argv, char **azColName)
{
	db_size = atoi(argv[0]);
	return 0;
}

static int size_from_db(struct expression *expr)
{
	char *name;

	if (!option_spammy)
		return 0;

	name = get_member_name(expr);
	if (!name)
		return 0;

	db_size = 0;
	run_sql(db_size_callback, "select size from type_size where type = '%s'",
			name);
	return db_size;
}

static int get_real_array_size(struct expression *expr)
{
	struct symbol *type;
	int ret;

	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return 0;

	ret = get_expression_value(type->array_size);
	/* Dynamically sized array are -1 in sparse */
	if (ret <= 0)
		return 0;
	/* People put one element arrays on the end of structs */
	if (ret == 1)
		return 0;

	return ret;
}

static int get_size_from_initializer(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol->initializer)
		return 0;
	if (expr->symbol->initializer == expr) /* int a = a; */
		return 0;
	return get_initializer_size(expr->symbol->initializer);
}

static int get_stored_size_bytes(struct expression *expr)
{
	struct sm_state *sm, *tmp;
	int max = 0;

	sm = get_sm_state_expr(my_size_id, expr);
	if (!sm)
		return 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (PTR_INT(tmp->state->data) > max)
			max = PTR_INT(tmp->state->data);
	} END_FOR_EACH_PTR(tmp);

	return max;
}

static int get_size_from_strlen(struct expression *expr)
{
	struct smatch_state *state;
	long long len;

	state = get_state_expr(my_strlen_id, expr);
	if (!state || !state->data)
		return 0;
	if (get_implied_max((struct expression *)state->data, &len))
		return len + 1; /* add one because strlen doesn't include the NULL */
	return 0;
}

int get_array_size_bytes(struct expression *expr)
{
	int size;

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	/* strcpy(foo, "BAR"); */
	if (expr->type == EXPR_STRING)
		return expr->string->length;

	/* buf[4] */
	size = get_real_array_size(expr);
	if (size)
		return elements_to_bytes(expr, size);

	/* buf = malloc(1024); */
	size = get_stored_size_bytes(expr);
	if (size)
		return size;

	/* char *foo = "BAR" */
	size = get_size_from_initializer(expr);
	if (size)
		return elements_to_bytes(expr, size);

	/* if (strlen(foo) > 4) */
	size = get_size_from_strlen(expr);
	if (size)
		return size;

	return size_from_db(expr);
}

int get_array_size(struct expression *expr)
{
	int bytes;

	bytes = get_array_size_bytes(expr);
	return bytes_to_elements(expr, bytes);
}

static void match_strlen_condition(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct expression *str = NULL;
	int strlen_left = 0;
	int strlen_right = 0;
	long long val;
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
		if (!get_value(right, &val))
			return;
	}
	if (strlen_right) {
		if (!get_value(left, &val))
			return;
	}

	if (expr->op == SPECIAL_EQUAL) {
		set_true_false_states_expr(my_size_id, str, alloc_state_num(val + 1), NULL);
		return;
	}
	if (expr->op == SPECIAL_NOTEQUAL) {
		set_true_false_states_expr(my_size_id, str, NULL, alloc_state_num(val + 1));
		return;
	}

	switch (expr->op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (strlen_left)
			true_state = alloc_state_num(val);
		else
			false_state = alloc_state_num(val + 1);
		break;
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LTE:
		if (strlen_left)
			true_state = alloc_state_num(val + 1);
		else
			false_state = alloc_state_num(val);
		break;
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		if (strlen_left)
			false_state = alloc_state_num(val);
		else
			true_state = alloc_state_num(val + 1);
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (strlen_left)
			false_state = alloc_state_num(val + 1);
		else
			true_state = alloc_state_num(val);
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

static void match_array_assignment(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	int array_size;

	if (expr->op != '=')
		return;
	left = strip_expr(expr->left);
	right = strip_expr(expr->right);
	right = strip_ampersands(right);
	array_size = get_array_size_bytes(right);
	if (array_size)
		set_state_expr(my_size_id, left, alloc_state_num(array_size));
}

static void info_record_alloction(struct expression *buffer, struct expression *size)
{
	char *member;
	long long val;

	if (!option_info)
		return;
	member = get_member_name(buffer);
	if (!member)
		return;
	if (!get_implied_value(size, &val))
		val = -1;
	sm_msg("info: '%s' allocated_buf_size %lld", member, val);
	free_string(member);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *right;
	struct expression *arg;
	long long bytes;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, size_arg);

	info_record_alloction(expr->left, arg);

	if (!get_implied_value(arg, &bytes))
		return;
	set_state_expr(my_size_id, expr->left, alloc_state_num(bytes));
}

static void match_calloc(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *arg;
	long long elements;
	long long size;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, 0);
	if (!get_implied_value(arg, &elements))
		return;
	arg = get_argument_from_call_expr(right->args, 1);
	if (!get_implied_value(arg, &size))
		return;
	set_state_expr(my_size_id, expr->left, alloc_state_num(elements * size));
}

static void match_strlen(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *str;
	struct expression *len_expr;
	char *len_name;
	struct smatch_state *state;

	right = strip_expr(expr->right);
	str = get_argument_from_call_expr(right->args, 0);
	len_expr = strip_expr(expr->left);

	len_name = get_variable_from_expr(len_expr, NULL);
	if (!len_name)
		return;

	state = __alloc_smatch_state(0);
	state->name = len_name;
	state->data = len_expr;
	set_state_expr(my_strlen_id, str, state);
}

static void match_limited(const char *fn, struct expression *expr, void *_limiter)
{
	struct limiter *limiter = (struct limiter *)_limiter;
	struct expression *dest;
	struct expression *size_expr;
	long long size;

	dest = get_argument_from_call_expr(expr->args, limiter->buf_arg);
	size_expr = get_argument_from_call_expr(expr->args, limiter->limit_arg);
	if (!get_implied_max(size_expr, &size))
		return;
	set_state_expr(my_size_id, dest, alloc_state_num(size));
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
	long long size;

	fn_expr = strip_expr(expr->right);
	size_expr = get_argument_from_call_expr(fn_expr->args, 1);
	if (!get_implied_max(size_expr, &size))
		return;

	/* It's easy to forget space for the NUL char */
	size++;
	set_state_expr(my_size_id, expr->left, alloc_state_num(size));
}

static void match_call(struct expression *expr)
{
	char *name;
	struct expression *arg;
	int bytes;
	int i;

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;

	i = 0;
	FOR_EACH_PTR(expr->args, arg) {
		bytes = get_array_size_bytes(arg);
		if (bytes > 1)
			sm_msg("info: passes_buffer '%s' %d '$$' %d %s",
				name, i, bytes,
				is_static(expr->fn) ? "static" : "global");
		i++;
	} END_FOR_EACH_PTR(arg);

	free_string(name);
}

static void struct_member_callback(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state)
{
	if (state == &merged)
		return;
	sm_msg("info: passes_buffer '%s' %d '%s' %s %s", fn, param, printed_name, state->name, global_static);
}

static void match_func_end(struct symbol *sym)
{
	memset(params_set, 0, sizeof(params_set));
}

void register_buf_size(int id)
{
	my_size_id = id;

	add_definition_db_callback(set_param_buf_size, BUF_SIZE);

	add_function_assign_hook("malloc", &match_alloc, INT_PTR(0));
	add_function_assign_hook("calloc", &match_calloc, NULL);
	add_function_assign_hook("memdup", &match_alloc, INT_PTR(1));
	if (option_project == PROJ_KERNEL) {
		add_function_assign_hook("kmalloc", &match_alloc, INT_PTR(0));
		add_function_assign_hook("kzalloc", &match_alloc, INT_PTR(0));
		add_function_assign_hook("vmalloc", &match_alloc, INT_PTR(0));
		add_function_assign_hook("__vmalloc", &match_alloc, INT_PTR(0));
		add_function_assign_hook("kcalloc", &match_calloc, NULL);
		add_function_assign_hook("drm_malloc_ab", &match_calloc, NULL);
		add_function_assign_hook("drm_calloc_large", &match_calloc, NULL);
		add_function_assign_hook("kmemdup", &match_alloc, INT_PTR(1));
		add_function_assign_hook("kmemdup_user", &match_alloc, INT_PTR(1));
	}
	add_hook(&match_array_assignment, ASSIGNMENT_HOOK);
	add_hook(&match_strlen_condition, CONDITION_HOOK);
	add_function_assign_hook("strlen", &match_strlen, NULL);

	add_function_assign_hook("strndup", match_strndup, NULL);
	if (option_project == PROJ_KERNEL)
		add_function_assign_hook("kstrndup", match_strndup, NULL);

	add_hook(&match_func_end, END_FUNC_HOOK);
	add_modification_hook(my_size_id, &set_undefined);
}

void register_strlen(int id)
{
	my_strlen_id = id;
	add_modification_hook(my_strlen_id, &set_undefined);
}

void register_buf_size_late(int id)
{
	add_function_hook("strlcpy", &match_limited, &b0_l2);
	add_function_hook("strlcat", &match_limited, &b0_l2);
	add_function_hook("memscan", &match_limited, &b0_l2);

	add_function_hook("strcpy", &match_strcpy, NULL);

	if (option_info) {
		add_hook(&match_call, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_size_id, struct_member_callback);
	}
}
