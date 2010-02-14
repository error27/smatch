/*
 * smatch/check_deference.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

struct bound {
	int param;
	int size;
};

/*
 * This check has two smatch IDs.  
 * my_size_id - used to store the size of arrays.  
 * my_used_id - keeps a record of array offsets that have been used.  
 *              If the code checks that they are within bounds later on,
 *              we complain about using an array offset before checking 
 *              that it is within bounds.
 */
static int my_size_id;
static int my_used_id;

static struct symbol *this_func;

static int get_array_size(struct expression *expr);

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

static void print_args(struct expression *expr, int size)
{
	struct symbol *sym;
	char *name;
	struct symbol *arg;
	const char *arg_name;
	int i;

	if (!option_info)
		return;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;

	i = 0;
	FOR_EACH_PTR(this_func->ctype.base_type->arguments, arg) {
		arg_name = (arg->ident?arg->ident->name:"-");
		if (sym == arg && !strcmp(name, arg_name)) {
			sm_info("param %d array index. size %d", i, size);
			goto free;
		}
		i++;
	} END_FOR_EACH_PTR(arg);
free:
	free_string(name);
}

static char *alloc_num(long long num)
{
	static char buff[256];

	if (num == whole_range.min)
		snprintf(buff, 255, "min");
	else if (num == whole_range.max)
		snprintf(buff, 255, "max");
	else if (num < 0)
		snprintf(buff, 255, "(%lld)", num);
	else
		snprintf(buff, 255, "%lld", num);

	buff[255] = '\0';
	return alloc_sname(buff);
}

static void delete(const char *name, struct symbol *sym, struct expression *expr, void *unused)
{
	delete_state(my_used_id, name, sym);
}

static struct smatch_state *alloc_my_state(int val)
{
	struct smatch_state *state;

	state = malloc(sizeof(*state));
	state->name = alloc_num(val);
	state->data = malloc(sizeof(int));
	*(int *)state->data = val;
	return state;
}

static int is_last_struct_member(struct expression *expr)
{
	struct ident *member;
	struct symbol *struct_sym;
	struct symbol *tmp;

	if (!expr || expr->type != EXPR_DEREF)
		return 0;

	member = expr->member;
	struct_sym = get_type(expr->deref);
	if (!struct_sym)
		return 0;
	if (struct_sym->type == SYM_PTR)
		struct_sym = get_base_type(struct_sym);
	FOR_EACH_PTR_REVERSE(struct_sym->symbol_list, tmp) {
		if (tmp->ident == member)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static int get_initializer_size(struct expression *expr)
{
	switch(expr->type) {
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

static int get_array_size(struct expression *expr)
{
	struct symbol *tmp;
	struct smatch_state *state;
	int ret = 0;

	if (expr->type == EXPR_STRING)
		return expr->string->length;

	tmp = get_type(expr);
	if (!tmp)
		return ret;

	if (tmp->type == SYM_ARRAY) {
		ret = get_expression_value(tmp->array_size);
		if (ret == 1 && is_last_struct_member(expr))
			return 0;
		if (ret)
			return ret;
	}

	if (expr->type == EXPR_SYMBOL && expr->symbol->initializer) {
		if (expr->symbol->initializer != expr) /* int a = a; */
			return get_initializer_size(expr->symbol->initializer);
	}
	state = get_state_expr(my_size_id, expr);
	if (!state || !state->data)
		return 0;
	if (tmp->type == SYM_PTR)
		tmp = get_base_type(tmp);
	if (!tmp->ctype.alignment)
		return 0;
	ret = *(int *)state->data / tmp->ctype.alignment;
	return ret;
}

static int definitely_just_used_as_limiter(struct expression *array, struct expression *offset)
{
	long long val;
	struct expression *tmp;
	int step = 0;
	int dot_ops = 0;

	if (!get_value(offset, &val))
		return 0;
	if (get_array_size(array) != val)
		return 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (step == 0) {
			step = 1;
			continue;
		}
		if (tmp->type == EXPR_PREOP && tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
		if (step == 1 && tmp->op == '&') {
			step = 2;
			continue;
		}
		if (step == 2 && tmp->type == EXPR_COMPARE)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static void array_check(struct expression *expr)
{
	struct expression *array_expr;
	int array_size;
	struct expression *offset;
	long long max;
	char *name;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	array_expr = get_array_name(expr);
	array_size = get_array_size(array_expr);
	if (!array_size)
		return;

	offset = get_array_offset(expr);
	if (!get_fuzzy_max(offset, &max)) {
		if (getting_address())
			return;
		set_state_expr(my_used_id, offset, alloc_state_num(array_size));
		add_modification_hook_expr(my_used_id, offset, &delete, NULL);
		print_args(offset, array_size);
	} else if (array_size <= max) {
		const char *level = "error";

		if (getting_address())
			level = "warn";

		if (definitely_just_used_as_limiter(array_expr, offset))
			return;

		name = get_variable_from_expr_complex(array_expr, NULL);
		/* Blast.  Smatch can't figure out glibc's strcmp __strcmp_cg()
		 * so it prints an error every time you compare to a string
		 * literal array with 4 or less chars.
		 */
		if (name && strcmp(name, "__s1") && strcmp(name, "__s2")) {
			sm_msg("%s: buffer overflow '%s' %d <= %lld", 
				level, name, array_size, max);
		}
		free_string(name);
	}
}

static void match_condition(struct expression *expr)
{
	int left;
	long long val;
	struct state_list *slist;
	struct sm_state *tmp;
	int boundary;

	if (!expr || expr->type != EXPR_COMPARE)
		return;
	if (get_implied_value(expr->left, &val))
		left = 1;
	else if (get_implied_value(expr->right, &val))
		left = 0;
	else
		return;

	if (left)
		slist = get_possible_states_expr(my_used_id, expr->right);
	else
		slist = get_possible_states_expr(my_used_id, expr->left);
	if (!slist)
		return;
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &merged)
			continue;
		boundary = (int)tmp->state->data;
		boundary -= val;
		if (boundary < 1 && boundary > -1) {
			char *name;

			name = get_variable_from_expr((left ? expr->right : expr->left), NULL);
			sm_msg("error: testing array offset '%s' after use.", name);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

static void match_string_assignment(struct expression *expr)
{
	struct expression *left;
	struct expression *right;

	left = strip_expr(expr->left);
	right = strip_expr(expr->right);
	if (right->type != EXPR_STRING || !right->string)
		return;
	set_state_expr(my_size_id, left, alloc_my_state(right->string->length));
}

static void match_array_assignment(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct symbol *left_type;
	int array_size;

	if (expr->op != '=')
		return;
	left = strip_expr(expr->left);
	left_type = get_type(left);
	if (!left_type || left_type->type != SYM_PTR)
		return;
	left_type = get_base_type(left_type);
	if (!left_type)
		return;
	right = strip_expr(expr->right);
	array_size = get_array_size(right);
	if (array_size)
		set_state_expr(my_size_id, left, 
			alloc_my_state(array_size * left_type->ctype.alignment));
}

static void match_malloc(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *arg;
	long long bytes;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, 0);
	if (!get_implied_value(arg, &bytes))
		return;
	set_state_expr(my_size_id, expr->left, alloc_my_state(bytes));
}

static void match_strcpy(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	char *data_name = NULL;
	struct smatch_state *dest_state;
	struct smatch_state *data_state;
	int dest_size;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, 1);
	data_name = get_variable_from_expr(data, NULL);

	dest_state = get_state(my_size_id, dest_name, NULL);
	if (!dest_state || !dest_state->data)
		goto free;

	data_state = get_state(my_size_id, data_name, NULL);
	if (!data_state || !data_state->data)
		goto free;
	dest_size = *(int *)dest_state->data;
	data_size = *(int *)data_state->data;
	if (dest_size < data_size)
		sm_msg("error: %s (%d) too large for %s (%d)", 
			data_name, data_size, dest_name, dest_size);
free:
	free_string(dest_name);
	free_string(data_name);
}

static void match_limitted(const char *fn, struct expression *expr, void *limit_arg)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	struct smatch_state *state;
	long long needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, PTR_INT(limit_arg));
	if (!get_value(data, &needed))
		goto free;
	state = get_state(my_size_id, dest_name, NULL);
	if (!state || !state->data)
		goto free;
	has = *(int *)state->data;
	if (has < needed)
		sm_msg("error: %s too small for %lld bytes.", dest_name, needed);
free:
	free_string(dest_name);
}

static void match_array_func(const char *fn, struct expression *expr, void *info)
{
	struct bound *bound_info = (struct bound *)info;
	struct expression *arg;
	long long offset;

	arg = get_argument_from_call_expr(expr->args, bound_info->param);
	if (!get_implied_value(arg, &offset))
		return;
	if (offset >= bound_info->size)
		sm_msg("error: buffer overflow calling %s. param %d.  %lld >= %d", 
			fn, bound_info->param, offset, bound_info->size);
}

static void register_array_funcs(void)
{
	struct token *token;
	const char *func;
	struct bound *bound_info;

	token = get_tokens_file("kernel.array_bounds");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		bound_info = malloc(sizeof(*bound_info));
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		bound_info->param = atoi(token->number);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		bound_info->size = atoi(token->number);
		add_function_hook(func, &match_array_func, bound_info);
		token = token->next;
	}
	clear_token_alloc();
}

void check_overflow(int id)
{
	my_size_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_string_assignment, ASSIGNMENT_HOOK);
	add_hook(&match_array_assignment, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_function_assign_hook("malloc", &match_malloc, NULL);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_limitted, (void *)2);
	if (option_project == PROJ_KERNEL) {
		add_function_assign_hook("kmalloc", &match_malloc, NULL);
		add_function_assign_hook("kzalloc", &match_malloc, NULL);
		add_function_assign_hook("vmalloc", &match_malloc, NULL);
		add_function_hook("copy_to_user", &match_limitted, (void *)2);
		add_function_hook("copy_from_user", &match_limitted, (void *)2);
	}
	register_array_funcs();
}

void register_check_overflow_again(int id)
{
	my_used_id = id;
}
