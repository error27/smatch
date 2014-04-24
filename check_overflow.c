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

static void delete(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_used_id, sm->name, sm->sym, &undefined);
}

static int definitely_just_used_as_limiter(struct expression *array, struct expression *offset)
{
	sval_t sval;
	struct expression *tmp;
	int step = 0;
	int dot_ops = 0;

	if (!get_implied_value(offset, &sval))
		return 0;
	if (get_array_size(array) != sval.value)
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
		if (step == 2 && tmp->type == EXPR_ASSIGNMENT)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static int get_the_max(struct expression *expr, sval_t *sval)
{
	if (get_hard_max(expr, sval))
		return 1;
	if (!option_spammy)
		return 0;
	if (get_fuzzy_max(expr, sval))
		return 1;
	if (is_user_data(expr))
		return get_absolute_max(expr, sval);
	return 0;
}

static int common_false_positives(char *name)
{
	if (!name)
		return 0;

	/* Smatch can't figure out glibc's strcmp __strcmp_cg()
	 * so it prints an error every time you compare to a string
	 * literal array with 4 or less chars.
	 */
	if (strcmp(name, "__s1") == 0 || strcmp(name, "__s2") == 0)
		return 1;

	/*
	 * passing WORK_CPU_UNBOUND is idiomatic but Smatch doesn't understand
	 * how it's used so it causes a bunch of false positives.
	 */
	if (option_project == PROJ_KERNEL &&
	    strcmp(name, "__per_cpu_offset") == 0)
		return 1;
	return 0;
}

static void array_check(struct expression *expr)
{
	struct expression *array_expr;
	int array_size;
	struct expression *offset;
	sval_t max;
	char *name;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	array_expr = strip_parens(expr->unop->left);
	array_size = get_array_size(array_expr);
	if (!array_size || array_size == 1)
		return;

	offset = get_array_offset(expr);
	if (!get_the_max(offset, &max)) {
		if (getting_address())
			return;
		if (is_capped(offset))
			return;
		set_state_expr(my_used_id, offset, alloc_state_num(array_size));
	} else if (array_size <= max.value) {
		const char *level = "error";

		if (getting_address())
			level = "warn";

		if (definitely_just_used_as_limiter(array_expr, offset))
			return;

		name = expr_to_str(array_expr);
		if (!common_false_positives(name)) {
			sm_msg("%s: buffer overflow '%s' %d <= %s",
				level, name, array_size, sval_to_str(max));
		}
		free_string(name);
	}
}

static void match_condition(struct expression *expr)
{
	int left;
	sval_t sval;
	struct state_list *slist;
	struct sm_state *tmp;
	int boundary;

	if (!expr || expr->type != EXPR_COMPARE)
		return;
	if (get_macro_name(expr->pos))
		return;
	if (get_implied_value(expr->left, &sval))
		left = 1;
	else if (get_implied_value(expr->right, &sval))
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
		boundary -= sval.value;
		if (boundary < 1 && boundary > -1) {
			char *name;

			name = expr_to_var(left ? expr->right : expr->left);
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

	dest_name = expr_to_str(dest);
	data_name = expr_to_str(data);

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
	sval_t limit_size;
	char *format;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_size_expr = get_argument_from_call_expr(expr->args, 1);
	format_string = get_argument_from_call_expr(expr->args, 2);
	data = get_argument_from_call_expr(expr->args, 3);

	dest_size = get_array_size_bytes(dest);
	if (!get_implied_value(dest_size_expr, &limit_size))
		return;
	if (dest_size && dest_size < limit_size.value)
		sm_msg("error: snprintf() is printing too much %s vs %d",
		       sval_to_str(limit_size), dest_size);
	format = expr_to_var(format_string);
	if (!format)
		return;
	if (strcmp(format, "\"%s\""))
		goto free;
	data_name = expr_to_str(data);
	data_size = get_array_size_bytes(data);
	if (limit_size.value < data_size)
		sm_msg("error: snprintf() chops off the last chars of '%s': %d vs %s",
		       data_name, data_size, sval_to_str(limit_size));
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
	format = expr_to_var(format_string);
	if (!format)
		return;
	if (strcmp(format, "\"%s\""))
		goto free;
	data_name = expr_to_str(data);
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
	sval_t needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, limiter->buf_arg);
	data = get_argument_from_call_expr(expr->args, limiter->limit_arg);
	if (!get_the_max(data, &needed))
		return;
	has = get_array_size_bytes_max(dest);
	if (!has)
		return;
	if (has >= needed.value)
		return;

	dest_name = expr_to_str(dest);
	sm_msg("error: %s() '%s' too small (%d vs %s)", fn, dest_name, has, sval_to_str(needed));
	free_string(dest_name);
}

static void db_returns_buf_size(struct expression *expr, int param, char *unused, char *math)
{
	struct expression *call;
	struct symbol *left_type, *right_type;
	int bytes;
	sval_t sval;

	if (expr->type != EXPR_ASSIGNMENT)
		return;
	right_type = get_pointer_type(expr->right);
	if (!right_type || type_bits(right_type) != -1)
		return;

	call = strip_expr(expr->right);
	left_type = get_pointer_type(expr->left);

	if (!parse_call_math(call, math, &sval) || sval.value == 0)
		return;
	if (!left_type)
		return;
	bytes = type_bytes(left_type);
	if (bytes <= 0)
		return;
	if (sval.uvalue >= bytes)
		return;
	sm_msg("error: not allocating enough data %d vs %s", bytes, sval_to_str(sval));
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
	select_return_states_hook(BUF_SIZE, &db_returns_buf_size);
	add_modification_hook(my_used_id, &delete);
	if (option_project == PROJ_KERNEL) {
		add_function_hook("copy_to_user", &match_limited, &b1_l2);
		add_function_hook("_copy_to_user", &match_limited, &b1_l2);
		add_function_hook("__copy_to_user", &match_limited, &b1_l2);
		add_function_hook("copy_from_user", &match_limited, &b0_l2);
		add_function_hook("_copy_from_user", &match_limited, &b0_l2);
		add_function_hook("__copy_from_user", &match_limited, &b0_l2);
	}
}
