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

struct limiter {
	int buf_arg;
	int limit_arg;
};
static struct limiter b0_l2 = {0, 2};
static struct limiter b1_l2 = {1, 2};

static int get_the_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	if (get_hard_max(expr, sval))
		return 1;
	if (!option_spammy)
		return 0;
	if (get_fuzzy_max(expr, sval))
		return 1;
	if (!is_user_data(expr))
		return 0;
	if (!get_user_rl(expr, &rl))
		return 0;
	*sval = rl_max(rl);
	return 1;
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

void check_memcpy_overflow(int id)
{
	register_funcs_from_file();
	add_function_hook("memcmp", &match_limited, &b0_l2);
	add_function_hook("memcmp", &match_limited, &b1_l2);
	if (option_project == PROJ_KERNEL) {
		add_function_hook("copy_to_user", &match_limited, &b1_l2);
		add_function_hook("_copy_to_user", &match_limited, &b1_l2);
		add_function_hook("__copy_to_user", &match_limited, &b1_l2);
		add_function_hook("copy_from_user", &match_limited, &b0_l2);
		add_function_hook("_copy_from_user", &match_limited, &b0_l2);
		add_function_hook("__copy_from_user", &match_limited, &b0_l2);
	}
}
