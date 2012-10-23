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

int absolute_id;

static char *show_num(long long num)
{
	static char buf[64];

	if (num < 0)
		sprintf(buf, "(%lld)", num);
	else
		sprintf(buf, "%lld", num);
	return buf;
}

static char *show_range(long long min, long long max)
{
	static char buf[256];
	char *p = buf;

	if (min == whole_range.min)
		p += sprintf(p, "min");
	else if (min == whole_range.max)
		p += sprintf(p, "max");
	else
		p += sprintf(p, "%s", show_num(min));
	if (min != max) {
		if (max == whole_range.max)
			sprintf(p, "-max");
		else
			sprintf(p, "-%s", show_num(max));
	}
	return buf;

}

static struct smatch_state *alloc_absolute(long long min, long long max)
{
	struct smatch_state *state;

	if (min == whole_range.min && max == whole_range.max)
		return &undefined;

	state = __alloc_smatch_state(0);
	state->name = alloc_string(show_range(min, max));
	state->data = alloc_range(min, max);
	return state;
}

static struct smatch_state *merge_func(struct smatch_state *s1, struct smatch_state *s2)
{
	struct data_range *r1, *r2;
	long long min, max;

	if (!s1->data || !s2->data)
		return &undefined;

	r1 = s1->data;
	r2 = s2->data;

	if (r1->min == r2->min && r1->max == r2->max)
		return s1;

	min = r1->min;
	if (r2->min < min)
		min = r2->min;
	max = r1->max;
	if (r2->max > max)
		max = r2->max;

	return alloc_absolute(min, max);
}

static void reset_state(struct sm_state *sm)
{
	set_state(absolute_id, sm->name, sm->sym, &undefined);
}

static void match_assign(struct expression *expr)
{
	struct symbol *type;
	long long min, max;

	if (expr->op != '=') {
		set_state_expr(absolute_id, expr->left, &undefined);
		return;
	}

	type = get_type(expr->left);
	if (!type)
		return;

	if (!get_absolute_min(expr->right, &min))
		min = whole_range.min;
	if (!get_absolute_max(expr->right, &max))
		max = whole_range.max;

	/* handle wrapping.  sort of sloppy */
	if (type_max(type) < max)
		min = type_min(type);
	if (type_min(type) > min)
		max = type_max(type);

	if (min <= type_min(type) && max >= type_max(type))
		set_state_expr(absolute_id, expr->left, &undefined);
	else
		set_state_expr(absolute_id, expr->left, alloc_absolute(min, max));
}

static void struct_member_callback(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state)
{
	struct data_range *range;

	if (!state->data)
		return;
	range = state->data;
	if (range->min == whole_range.min && range->max == whole_range.max)
		return;
	sm_msg("info: passes absolute_limits '%s' %d '%s' %s %s", fn, param, printed_name, state->name, global_static);
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	char *name;
	int i = 0;

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;

	FOR_EACH_PTR(expr->args, arg) {
		long long min, max;

		if (!get_absolute_min(arg, &min))
			continue;
		if (!get_absolute_max(arg, &max))
			continue;
		if (min == whole_range.min && max == whole_range.max)
			continue;

		/* fixme: determine the type of the paramter */
		sm_msg("info: passes absolute_limits '%s' %d '$$' %s %s",
		       name, i, show_range(min, max),
		       is_static(expr->fn) ? "static" : "global");
		i++;
	} END_FOR_EACH_PTR(arg);

	free_string(name);
}

static void set_param_limits(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	long long min, max;
	char fullname[256];

	if (strncmp(key, "$$", 2))
		return;

	snprintf(fullname, 256, "%s%s", name, key + 2);
	get_value_ranges(value, &rl);
	min = rl_min(rl);
	max = rl_max(rl);
	set_state(absolute_id, fullname, sym, alloc_absolute(min, max));
}

void register_absolute(int id)
{
	absolute_id = id;

	add_merge_hook(absolute_id, &merge_func);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	if (option_info) {
		add_hook(&match_call_info, FUNCTION_CALL_HOOK);
		add_member_info_callback(absolute_id, struct_member_callback);
	}
	add_definition_db_callback(set_param_limits, ABSOLUTE_LIMITS);
}

void register_absolute_late(int id)
{
	add_modification_hook(absolute_id, reset_state);
}
