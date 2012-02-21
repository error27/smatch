/*
 * smatch/smatch_dinfo.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * smatch_dinfo.c has helper functions for handling data_info structs
 *
 */

#include <stdlib.h>
#ifndef __USE_ISOC99
#define __USE_ISOC99
#endif
#include <limits.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

struct data_range whole_range = {
	.min = LLONG_MIN,
	.max = LLONG_MAX,
};

struct data_info *get_dinfo(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return (struct data_info *)state->data;

}

struct range_list *estate_ranges(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return get_dinfo(state)->value_ranges;
}

struct related_list *estate_related(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return get_dinfo(state)->related;
}

long long estate_min(struct smatch_state *state)
{
	return rl_min(estate_ranges(state));
}

long long estate_max(struct smatch_state *state)
{
	return rl_max(estate_ranges(state));
}

static struct data_info *alloc_dinfo(void)
{
	struct data_info *ret;

	ret = __alloc_data_info(0);
	ret->related = NULL;
	ret->type = DATA_RANGE;
	ret->value_ranges = NULL;
	return ret;
}

static struct data_info *alloc_dinfo_range(long long min, long long max)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	add_range(&ret->value_ranges, min, max);
	return ret;
}

static struct data_info *alloc_dinfo_range_list(struct range_list *rl)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	ret->value_ranges = rl;
	return ret;
}

static struct data_info *clone_dinfo(struct data_info *dinfo)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	ret->related = clone_related_list(dinfo->related);
	ret->value_ranges = clone_range_list(dinfo->value_ranges);
	return ret;
}

struct smatch_state *clone_estate(struct smatch_state *state)
{
	struct smatch_state *ret;

	ret = __alloc_smatch_state(0);
	ret->name = state->name;
	ret->data = clone_dinfo(get_dinfo(state));
	return ret;
}

struct smatch_state *alloc_estate_empty(void)
{
	struct smatch_state *state;
	struct data_info *dinfo;

	dinfo = alloc_dinfo();
	state = __alloc_smatch_state(0);
	state->data = dinfo;
	state->name = "";
	return state;
}

static struct smatch_state *alloc_estate_no_name(int val)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range(val, val);
	return state;
}

/* We do this because ->value_ranges is a list */
struct smatch_state *extra_undefined(void)
{
	struct data_info *dinfo;
	static struct smatch_state *ret;
	static struct symbol *prev_func;

	if  (prev_func == cur_func_sym)
		return ret;
	prev_func = cur_func_sym;

	dinfo = alloc_dinfo_range(whole_range.min, whole_range.max);
	ret = __alloc_smatch_state(0);
	ret->name = "unknown";
	ret->data = dinfo;
	return ret;
}

struct smatch_state *extra_empty(void)
{
	static struct smatch_state *ret;
	static struct symbol *prev_func;

	if  (prev_func == cur_func_sym)
		return ret;
	prev_func = cur_func_sym;

	ret = __alloc_smatch_state(0);
	ret->name = "empty";
	ret->data = alloc_dinfo();
	return ret;
}

struct smatch_state *alloc_estate(long long val)
{
	struct smatch_state *state;

	state = alloc_estate_no_name(val);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

struct smatch_state *alloc_estate_range(long long min, long long max)
{
	struct smatch_state *state;

	if (min == whole_range.min && max == whole_range.max)
		return extra_undefined();
	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range(min, max);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

struct smatch_state *alloc_estate_range_list(struct range_list *rl)
{
	struct smatch_state *state;

	if (!rl)
		return extra_empty();

	if (is_whole_range_rl(rl))
		return extra_undefined();

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range_list(rl);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

