/*
 * sparse/check_snprintf.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static void ok_to_use(struct sm_state *sm)
{
	delete_state(my_id, sm->name, sm->sym);
}

static void match_snprintf(const char *fn, struct expression *expr, void *info)
{
	struct expression *call;
	struct expression *arg;
	long long buflen;

	call = strip_expr(expr->right);
	arg = get_argument_from_call_expr(call->args, 1);
	if (!get_fuzzy_max(arg, &buflen))
		return;
	set_state_expr(my_id, expr->left, alloc_state_num(buflen));
}

static int get_old_buflen(struct sm_state *sm)
{
	struct sm_state *tmp;
	int ret = 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (PTR_INT(tmp->state->data) > ret)
			ret = PTR_INT(tmp->state->data);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	struct sm_state *sm;
	int old_buflen;
	long long max;

	FOR_EACH_PTR(expr->args, arg) {
		sm = get_sm_state_expr(my_id, arg);
		if (!sm)
			continue;
		old_buflen = get_old_buflen(sm);
		if (!old_buflen)
			return;
		if (get_absolute_max(arg, &max) && max > old_buflen)
			sm_msg("warn: '%s' returned from snprintf() might be larger than %d",
				sm->name, old_buflen);
	} END_FOR_EACH_PTR(arg);
}

void check_snprintf(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	if (!option_spammy)
		return;

	my_id = id;
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_function_assign_hook("snprintf", &match_snprintf, NULL);
	add_modification_hook(my_id, &ok_to_use);
}

