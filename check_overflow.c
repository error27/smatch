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
static struct limiter b0_l2 = {0, 2};
static struct limiter b1_l2 = {1, 2};

static void delete(struct sm_state *sm)
{
	set_state(my_used_id, sm->name, sm->sym, &undefined);
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
	if (get_macro_name(expr->pos))
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
		if (tmp->state == &merged || tmp->state == &undefined)
			continue;
		boundary = PTR_INT(tmp->state->data);
		boundary -= val;
		if (boundary < 1 && boundary > -1) {
			char *name;

			name = get_variable_from_expr((left ? expr->right : expr->left), NULL);
			sm_msg("error: testing array offset '%s' after use.", name);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
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
	else if (option_spammy)
		sm_msg("warn: %s() '%s' of unknown size might be too large for '%s'",
			fn, data_name, dest_name);

	free_string(dest_name);
	free_string(data_name);
}

static void match_snprintf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *dest_size_expr;
	struct expression *format_string;
	struct expression *data;
	char *data_name = NULL;
	int dest_size;
	long long limit_size;
	char *format;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_size_expr = get_argument_from_call_expr(expr->args, 1);
	format_string = get_argument_from_call_expr(expr->args, 2);
	data = get_argument_from_call_expr(expr->args, 3);

	dest_size = get_array_size_bytes(dest);
	if (!get_implied_value(dest_size_expr, &limit_size))
		return;
	if (dest_size && dest_size < limit_size)
		sm_msg("error: snprintf() is printing too much %lld vs %d", limit_size, dest_size);
	format = get_variable_from_expr(format_string, NULL);
	if (!format)
		return;
	if (strcmp(format, "\"%s\""))
		goto free;
	data_name = get_variable_from_expr_complex(data, NULL);
	data_size = get_array_size_bytes(data);
	if (limit_size < data_size)
		sm_msg("error: snprintf() chops off the last chars of '%s': %d vs %lld",
		       data_name, data_size, limit_size);
free:
	free_string(data_name);
	free_string(format);
}

static void match_sprintf(const char *fn, struct expression *expr, void *unused)
{
	struct expression *dest;
	struct expression *format_string;
	struct expression *data;
	char *data_name = NULL;
	int dest_size;
	char *format;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	format_string = get_argument_from_call_expr(expr->args, 1);
	data = get_argument_from_call_expr(expr->args, 2);

	dest_size = get_array_size_bytes(dest);
	if (!dest_size)
		return;
	format = get_variable_from_expr(format_string, NULL);
	if (!format)
		return;
	if (strcmp(format, "\"%s\""))
		goto free;
	data_name = get_variable_from_expr_complex(data, NULL);
	data_size = get_array_size_bytes(data);
	if (dest_size < data_size)
		sm_msg("error: sprintf() copies too much data from '%s': %d vs %d",
		       data_name, data_size, dest_size);
free:
	free_string(data_name);
	free_string(format);
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

static void register_funcs_from_file(void)
{
	char name[256];
	struct token *token;
	const char *func;
	int size, buf;
	struct limiter *limiter;

	snprintf(name, 256, "%s.sizeof_param", option_project_str);
	name[255] = '\0';
	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);

		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		size = atoi(token->number);

		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		buf = atoi(token->number);

		limiter = malloc(sizeof(*limiter));
		limiter->limit_arg = size;
		limiter->buf_arg = buf;

		add_function_hook(func, &match_limited, limiter);

		token = token->next;
	}
	clear_token_alloc();
}

void check_overflow(int id)
{
	my_used_id = id;
	register_funcs_from_file();
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("snprintf", &match_snprintf, NULL);
	add_function_hook("sprintf", &match_sprintf, NULL);
	add_function_hook("memcmp", &match_limited, &b0_l2);
	add_function_hook("memcmp", &match_limited, &b1_l2);
	add_modification_hook(my_used_id, &delete);
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
	}
}
