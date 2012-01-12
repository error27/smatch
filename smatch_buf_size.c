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

static float get_cast_ratio(struct expression *unstripped)
{
	struct expression *start_expr;
	struct symbol *start_type;
	struct symbol *end_type;
	int start_bytes = 0;
	int end_bytes = 0;

	start_expr = strip_expr(unstripped);
	start_type  =  get_type(start_expr);
	end_type = get_type(unstripped);
	if (!start_type || !end_type)
		return 1;

	if (start_type->type == SYM_PTR)
		start_bytes = (get_base_type(start_type))->ctype.alignment;
	if (start_type->type == SYM_ARRAY)
		start_bytes = (get_base_type(start_type))->bit_size / 8;
	if (end_type->type == SYM_PTR)
		end_bytes = (get_base_type(end_type))->ctype.alignment;
	if (end_type->type == SYM_ARRAY)
		end_bytes = (get_base_type(end_type))->bit_size / 8;

	if (!start_bytes || !end_bytes)
		return 1;
	return start_bytes / end_bytes;
}

int get_array_size(struct expression *expr)
{
	struct symbol *tmp;
	struct smatch_state *state;
	int ret = 0;
	float cast_ratio;
	long long len;

	if (expr->type == EXPR_STRING)
		return expr->string->length;

	cast_ratio = get_cast_ratio(expr);
	expr = strip_expr(expr);
	tmp = get_type(expr);
	if (!tmp)
		return 0;

	if (tmp->type == SYM_ARRAY) {
		ret = get_expression_value(tmp->array_size);
		/* Dynamically sized array are -1 in sparse */
		if (ret < 0)
			return 0;
		/* People put one element arrays on the end of structs */
		if (ret == 1)
			return 0;
		if (ret)
			return ret * cast_ratio;
	}

	state = get_state_expr(my_size_id, expr);
	if (state == &merged)
		return 0;
	if (state && state->data) {
		if (tmp->type == SYM_PTR)
			tmp = get_base_type(tmp);
		if (!tmp->ctype.alignment)
			return 0;
		ret = PTR_INT(state->data) / tmp->ctype.alignment;
		return ret * cast_ratio;
	}

	if (expr->type == EXPR_SYMBOL && expr->symbol->initializer) {
		if (expr->symbol->initializer != expr) /* int a = a; */
			return get_initializer_size(expr->symbol->initializer) * cast_ratio;
	}

	state = get_state_expr(my_strlen_id, expr);
	if (!state || !state->data)
		return 0;
	if (get_implied_max((struct expression *)state->data, &len))
		return len + 1; /* add one because strlen doesn't include the NULL */
	return 0;
}

int get_array_size_bytes(struct expression *expr)
{
	struct symbol *tmp;
	int array_size;
	int element_size;

	if (expr->type == EXPR_STRING)
		return expr->string->length;

	tmp = get_type(expr);
	if (!tmp)
		return 0;

	if (tmp->type == SYM_ARRAY) {
		tmp = get_base_type(tmp);
		element_size = tmp->bit_size / 8;
	} else if (tmp->type == SYM_PTR) {
		tmp = get_base_type(tmp);
		element_size = tmp->ctype.alignment;
	} else {
		return 0;
	}
	array_size = get_array_size(expr);
	return array_size * element_size;
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

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *right;
	struct expression *arg;
	long long bytes;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, size_arg);
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
			sm_msg("info: passes_buffer '%s' %d '$$' %d", name, i, bytes);
		i++;
	} END_FOR_EACH_PTR(arg);

	free_string(name);
}

static void struct_member_callback(char *fn, int param, char *printed_name, struct smatch_state *state)
{
	if (state == &merged)
		return;
	sm_msg("info: passes_buffer '%s' %d '%s' %s", fn, param, printed_name, state->name);
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

	add_function_hook("strlcpy", &match_limited, &b0_l2);
	add_function_hook("strlcat", &match_limited, &b0_l2);
	add_function_hook("memscan", &match_limited, &b0_l2);

	add_function_hook("strcpy", &match_strcpy, NULL);

	add_function_assign_hook("strndup", match_strndup, NULL);
	if (option_project == PROJ_KERNEL)
		add_function_assign_hook("kstrndup", match_strndup, NULL);

	if (option_info) {
		add_hook(&match_call, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_size_id, struct_member_callback);
	}

	add_hook(&match_func_end, END_FUNC_HOOK);
}

void register_strlen(int id)
{
	my_strlen_id = id;
}
