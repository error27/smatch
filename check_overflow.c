/*
 * smatch/check_overflow.c
 *
 * Copyright (C) 2010 Dan Carpenter.
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
 * my_used_id - keeps a record of array offsets that have been used.  
 *              If the code checks that they are within bounds later on,
 *              we complain about using an array offset before checking 
 *              that it is within bounds.
 */
static int my_used_id;

static struct symbol *this_func;

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

struct limiter {
	int buf_arg;
	int limit_arg;
};
struct limiter b0_l2 = {0, 2};
struct limiter b1_l2 = {1, 2};

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

	state = __alloc_smatch_state(0);
	state->name = alloc_num(val);
	state->data = malloc(sizeof(int));
	*(int *)state->data = val;
	return state;
}

static int definitely_just_used_as_limiter(struct expression *array, struct expression *offset)
{
	long long val;
	struct expression *tmp;
	int step = 0;
	int dot_ops = 0;

	if (!get_implied_value(offset, &val))
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

	array_expr = strip_parens(expr->unop->left);
	array_size = get_array_size(array_expr);
	if (!array_size || array_size == 1)
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

		if (!option_spammy) {
			struct smatch_state *state;
			
			state = get_state_expr(SMATCH_EXTRA, offset);
			if (state && is_whole_range(state))
				return;
		}

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

static void match_strcpy(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	char *data_name = NULL;
	int dest_size;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	data = get_argument_from_call_expr(expr->args, 1);
	dest_size = get_array_size_bytes(dest);
	data_size = get_array_size_bytes(data);

	if (!dest_size)
		return;

	/* If the size of both arrays is known and the destination
	 * buffer is larger than the source buffer, we're okay.
	 */
	if (data_size && dest_size >= data_size)
		return;

	dest_name = get_variable_from_expr_complex(dest, NULL);
	data_name = get_variable_from_expr_complex(data, NULL);

	if (data_size)
		sm_msg("error: %s() '%s' too large for '%s' (%d vs %d)",
			fn, data_name, dest_name, data_size, dest_size);
	else
		sm_msg("warn: %s() '%s' of unknown size might be too large for '%s'",
			fn, data_name, dest_name);

	free_string(dest_name);
	free_string(data_name);
}

static void match_limited(const char *fn, struct expression *expr, void *_limiter)
{
	struct limiter *limiter = (struct limiter *)_limiter;
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	long long needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, limiter->buf_arg);
	data = get_argument_from_call_expr(expr->args, limiter->limit_arg);
	if (!get_fuzzy_max(data, &needed))
		return;
	has = get_array_size_bytes(dest);
	if (!has)
		return;
	if (has >= needed)
		return;

	dest_name = get_variable_from_expr_complex(dest, NULL);
	sm_msg("error: %s() '%s' too small (%d vs %lld)", fn, dest_name, has, needed);
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
	struct bound *bound_info = NULL;

	token = get_tokens_file("kernel.array_bounds");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		bound_info = malloc(sizeof(*bound_info));
		if (token_type(token) != TOKEN_IDENT)
			break;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			break;
		bound_info->param = atoi(token->number);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			break;
		bound_info->size = atoi(token->number);
		add_function_hook(func, &match_array_func, bound_info);
		token = token->next;
	}
	if (token_type(token) != TOKEN_STREAMEND)
		free(bound_info);
	clear_token_alloc();
}

void check_overflow(int id)
{
	my_used_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_limited, &b0_l2);
	add_function_hook("memset", &match_limited, &b0_l2);
	if (option_project == PROJ_KERNEL) {
		add_function_hook("copy_to_user", &match_limited, &b0_l2);
		add_function_hook("copy_to_user", &match_limited, &b1_l2);
		add_function_hook("_copy_to_user", &match_limited, &b0_l2);
		add_function_hook("_copy_to_user", &match_limited, &b1_l2);
		add_function_hook("__copy_to_user", &match_limited, &b0_l2);
		add_function_hook("__copy_to_user", &match_limited, &b1_l2);
		add_function_hook("copy_from_user", &match_limited, &b0_l2);
		add_function_hook("copy_from_user", &match_limited, &b1_l2);
		add_function_hook("_copy_from_user", &match_limited, &b0_l2);
		add_function_hook("_copy_from_user", &match_limited, &b1_l2);
		add_function_hook("__copy_from_user", &match_limited, &b0_l2);
		add_function_hook("__copy_from_user", &match_limited, &b1_l2);
		add_function_hook("__builtin_memset", &match_limited, &b0_l2);
	}
	register_array_funcs();
}
