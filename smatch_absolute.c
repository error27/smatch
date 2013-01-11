/*
 * smatch/smatch_absolute.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 * This is to track the absolute max that variables can be.  It's a bit like
 * smatch_extra.c but it only tracks the absolute max and min.  So for example,
 * if you have "int x = (unsigned char)y;" then the absolute max of x is 255.
 *
 * I imagine this will be useful for find integer overflows.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static const char *show_range(sval_t min, sval_t max)
{
	static char buf[256];

	if (sval_cmp(min, max) == 0)
		return sval_to_str(min);
	snprintf(buf, sizeof(buf), "%s-%s", sval_to_str(min), sval_to_str(max));
	return buf;

}

static struct smatch_state *alloc_absolute(sval_t min, sval_t max)
{
	struct smatch_state *state;

	if (sval_is_min(min) && sval_is_max(max))
		return &undefined;

	state = __alloc_smatch_state(0);
	state->name = alloc_string(show_range(min, max));
	state->data = alloc_range(min, max);
	return state;
}

static struct smatch_state *merge_func(struct smatch_state *s1, struct smatch_state *s2)
{
	struct data_range *r1, *r2;
	sval_t min, max;

	if (!s1->data || !s2->data)
		return &undefined;

	r1 = s1->data;
	r2 = s2->data;

	if (r1->min.value == r2->min.value && r1->max.value == r2->max.value)
		return s1;

	min = r1->min;
	if (sval_cmp(r2->min, min) < 0)
		min = r2->min;
	max = r1->max;
	if (sval_cmp(r2->max, max) > 0)
		max = r2->max;

	return alloc_absolute(min, max);
}

static void reset_state(struct sm_state *sm)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

static void match_assign(struct expression *expr)
{
	struct symbol *left_type;
	sval_t min, max;
	struct range_list *rl;

	if (expr->op != '=') {
		set_state_expr(my_id, expr->left, &undefined);
		return;
	}

	left_type = get_type(expr->left);
	if (!left_type)
		return;

	get_absolute_min(expr->right, &min);
	get_absolute_max(expr->right, &max);

	rl = alloc_rl(min, max);
	rl = cast_rl(left_type, rl);

	min = rl_min(rl);
	max = rl_max(rl);
	set_state_expr(my_id, expr->left, alloc_absolute(min, max));
}

static void struct_member_callback(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state)
{
	struct data_range *range;

	if (!state->data)
		return;
	range = state->data;
	if (sval_is_min(range->min) && sval_is_max(range->max))
		return;
	sm_msg("info: passes absolute_limits '%s' %d '%s' %s %s", fn, param, printed_name, state->name, global_static);
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	char *name;
	int i;

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		sval_t min, max;

		i++;

		if (!get_absolute_min(arg, &min))
			continue;
		if (!get_absolute_max(arg, &max))
			continue;
		if (sval_is_min(min) && sval_is_max(max))
			continue;

		/* fixme: determine the type of the paramter */
		sm_msg("info: passes absolute_limits '%s' %d '$$' %s %s",
		       name, i, show_range(min, max),
		       is_static(expr->fn) ? "static" : "global");
	} END_FOR_EACH_PTR(arg);

	free_string(name);
}

static void set_param_limits(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	sval_t min, max;
	char fullname[256];

	if (strncmp(key, "$$", 2))
		return;

	snprintf(fullname, 256, "%s%s", name, key + 2);
	str_to_rl(get_real_base_type(sym), value, &rl);
	min = rl_min(rl);
	max = rl_max(rl);
	set_state(my_id, fullname, sym, alloc_absolute(min, max));
}

int get_absolute_min_helper(struct expression *expr, sval_t *sval)
{
	struct smatch_state *state;
	struct data_range *range;

	if (get_implied_min(expr, sval))
		return 1;

	state = get_state_expr(my_id, expr);
	if (!state || !state->data)
		return 0;

	range = state->data;
	*sval = range->min;
	return 1;
}

int get_absolute_max_helper(struct expression *expr, sval_t *sval)
{
	struct smatch_state *state;
	struct data_range *range;

	if (get_implied_max(expr, sval))
		return 1;

	state = get_state_expr(my_id, expr);
	if (!state || !state->data)
		return 0;

	range = state->data;
	*sval = range->max;
	return 1;
}

void register_absolute(int id)
{
	my_id = id;

	add_merge_hook(my_id, &merge_func);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	if (option_info) {
		add_hook(&match_call_info, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_id, struct_member_callback);
	}
	add_definition_db_callback(set_param_limits, ABSOLUTE_LIMITS);
}

void register_absolute_late(int id)
{
	add_modification_hook(my_id, reset_state);
}
