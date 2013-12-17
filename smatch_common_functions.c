/*
 * smatch/smatch_common_functions.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_extra.h"

static int match_strlen(struct expression *call, void *unused, struct range_list **rl)
{
	struct expression *str;
	unsigned long max;

	str = get_argument_from_call_expr(call->args, 0);
	if (get_implied_strlen(str, rl) && sval_is_positive(rl_min(*rl))) {
		*rl = cast_rl(&ulong_ctype, *rl);
		return 1;
	}
	/* smatch_strlen.c is not very complete */
	max = get_array_size_bytes_max(str);
	if (max == 0) {
		*rl = alloc_whole_rl(&ulong_ctype);
	} else {
		max--;
		*rl = alloc_rl(ll_to_sval(0), ll_to_sval(max));
	}
	return 1;
}

static int match_strnlen(struct expression *call, void *unused, struct range_list **rl)
{
	struct expression *limit;
	sval_t fixed;
	sval_t bound;
	sval_t ulong_max = sval_type_val(&ulong_ctype, ULONG_MAX);

	match_strlen(call, NULL, rl);
	limit = get_argument_from_call_expr(call->args, 1);
	if (!get_implied_max(limit, &bound))
		return 1;
	if (sval_cmp(bound, ulong_max) == 0)
		return 1;
	if (rl_to_sval(*rl, &fixed) && sval_cmp(fixed, bound) >= 0) {
		*rl = alloc_rl(bound, bound);
		return 1;
	}

	bound.value++;
	*rl = remove_range(*rl, bound, ulong_max);

	return 1;
}

void register_common_functions(int id)
{
	add_implied_return_hook("strlen", &match_strlen, NULL);
	add_implied_return_hook("strnlen", &match_strnlen, NULL);
}
