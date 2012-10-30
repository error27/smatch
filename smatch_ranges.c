/*
 * sparse/smatch_ranges.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "parse.h"
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

ALLOCATOR(data_info, "smatch extra data");
ALLOCATOR(data_range, "data range");
__DO_ALLOCATOR(struct data_range, sizeof(struct data_range), __alignof__(struct data_range),
			 "permanent ranges", perm_data_range);
ALLOCATOR(data_range_sval, "data range sval");
__DO_ALLOCATOR(struct data_range_sval, sizeof(struct data_range_sval), __alignof__(struct data_range_sval),
			 "permanent ranges sval", perm_data_range_sval);

struct data_range whole_range = {
	.min = LLONG_MIN,
	.max = LLONG_MAX,
};

static char *show_num(long long num)
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
	return buff;
}

char *show_ranges(struct range_list *list)
{
	struct data_range *tmp;
	char full[256];
	int i = 0;

	full[0] = '\0';
	full[255] = '\0';
	FOR_EACH_PTR(list, tmp) {
		if (i++)
			strncat(full, ",", 254 - strlen(full));
		if (tmp->min == tmp->max) {
			strncat(full, show_num(tmp->min), 254 - strlen(full));
			continue;
		}
		strncat(full, show_num(tmp->min), 254 - strlen(full));
		strncat(full, "-", 254 - strlen(full));
		strncat(full, show_num(tmp->max), 254 - strlen(full));
	} END_FOR_EACH_PTR(tmp);
	return alloc_sname(full);
}

void get_value_ranges(char *value, struct range_list **rl)
{
	long long val1, val2;
	char *start;
	char *c;

	*rl = NULL;

	c = value;
	while (*c) {
		if (*c == '(')
			c++;
		start = c;

		if (!strncmp(start, "max", 3)) {
			val1 = LLONG_MAX;
			c += 3;
		} else if (!strncmp(start, "u64max", 6)) {
			val1 = LLONG_MAX; // FIXME
			c += 6;
		} else if (!strncmp(start, "s64max", 6)) {
			val1 = LLONG_MAX;
			c += 6;
		} else if (!strncmp(start, "u32max", 6)) {
			val1 = UINT_MAX;
			c += 6;
		} else if (!strncmp(start, "s32max", 6)) {
			val1 = INT_MAX;
			c += 6;
		} else if (!strncmp(start, "u16max", 6)) {
			val1 = USHRT_MAX;
			c += 6;
		} else if (!strncmp(start, "s16max", 6)) {
			val1 = SHRT_MAX;
			c += 6;
		} else if (!strncmp(start, "min", 3)) {
			val1 = LLONG_MIN;
			c += 3;
		} else if (!strncmp(start, "s64min", 6)) {
			val1 = LLONG_MIN;
			c += 6;
		} else if (!strncmp(start, "s32min", 6)) {
			val1 = INT_MIN;
			c += 6;
		} else if (!strncmp(start, "s16min", 6)) {
			val1 = SHRT_MIN;
			c += 6;
		} else {
			while (*c && *c != ',' && *c != '-')
				c++;
			val1 = strtoll(start, &c, 10);
		}
		if (*c == ')')
			c++;
		if (!*c) {
			add_range(rl, val1, val1);
			break;
		}
		if (*c == ',') {
			add_range(rl, val1, val1);
			c++;
			start = c;
			continue;
		}
		c++; /* skip the dash in eg. 4-5 */
		if (*c == '(')
			c++;
		start = c;
		if (!strncmp(start, "max", 3)) {
			val2 = LLONG_MAX;
			c += 3;
		} else {

			while (*c && *c != ',' && *c != '-')
				c++;
			val2 = strtoll(start, &c, 10);
		}
		add_range(rl, val1, val2);
		if (!*c)
			break;
		if (*c == ')')
			c++;
		c++; /* skip the comma in eg: 4-5,7 */
	}
}

static struct data_range range_zero = {
	.min = 0,
	.max = 0,
};

static struct data_range range_one = {
	.min = 1,
	.max = 1,
};

int is_whole_range_rl(struct range_list *rl)
{
	struct data_range *drange;

	if (ptr_list_empty(rl))
		return 1;
	drange = first_ptr_list((struct ptr_list *)rl);
	if (drange->min == whole_range.min && drange->max == whole_range.max)
		return 1;
	return 0;
}

int rl_contiguous(struct range_list *rl)
{
	if (first_ptr_list((struct ptr_list *)rl) == last_ptr_list((struct ptr_list *)rl))
		return 1;
	return 0;
}

long long rl_min(struct range_list *rl)
{
	struct data_range *drange;

	if (ptr_list_empty(rl))
		return whole_range.min;
	drange = first_ptr_list((struct ptr_list *)rl);
	return drange->min;
}

sval_t rl_min_sval(struct range_list *rl)
{
	struct data_range *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MIN;
	if (ptr_list_empty(rl))
		return ret;
	drange = first_ptr_list((struct ptr_list *)rl);
	ret.value = drange->min;
	return ret;
}

long long rl_max(struct range_list *rl)
{
	struct data_range *drange;

	if (ptr_list_empty(rl))
		return whole_range.max;
	drange = last_ptr_list((struct ptr_list *)rl);
	return drange->max;
}

sval_t rl_max_sval(struct range_list *rl)
{
	struct data_range *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MAX;
	if (ptr_list_empty(rl))
		return ret;
	drange = last_ptr_list((struct ptr_list *)rl);
	ret.value = drange->max;
	return ret;
}

static struct data_range *alloc_range_helper(long long min, long long max, int perm)
{
	struct data_range *ret;

	if (min > max) {
		// sm_msg("debug invalid range %lld to %lld", min, max);
		min = whole_range.min;
		max = whole_range.max;
	}
	if (min == whole_range.min && max == whole_range.max)
		return &whole_range;
	if (min == 0 && max == 0)
		return &range_zero;
	if (min == 1 && max == 1)
		return &range_one;

	if (perm)
		ret = __alloc_perm_data_range(0);
	else
		ret = __alloc_data_range(0);
	ret->min = min;
	ret->max = max;
	return ret;
}

static struct data_range_sval *alloc_range_helper_sval(sval_t min, sval_t max, int perm)
{
	struct data_range_sval *ret;

	if (sval_cmp(min, max) > 0) {
		// sm_msg("debug invalid range %lld to %lld", min, max);
		min.value = LLONG_MIN;  /* fixme: need a way to represent unknown svals */
		max.value = LLONG_MAX;
	}

	if (perm)
		ret = __alloc_perm_data_range_sval(0);
	else
		ret = __alloc_data_range_sval(0);
	ret->min = min;
	ret->max = max;
	return ret;
}

struct data_range *alloc_range(long long min, long long max)
{
	return alloc_range_helper(min, max, 0);
}

struct data_range_sval *alloc_range_sval(sval_t min, sval_t max)
{
	return alloc_range_helper_sval(min, max, 0);
}

struct data_range *alloc_range_perm(long long min, long long max)
{
	return alloc_range_helper(min, max, 1);
}

struct range_list *alloc_range_list(long long min, long long max)
{
	struct range_list *rl = NULL;

	add_range(&rl, min, max);
	return rl;
}

struct range_list *whole_range_list(void)
{
	return alloc_range_list(whole_range.min, whole_range.max);
}

void add_range(struct range_list **list, long long min, long long max)
{
	struct data_range *tmp = NULL;
	struct data_range *new = NULL;
	int check_next = 0;

	/*
	 * FIXME:  This has a problem merging a range_list like: min-0,3-max
	 * with a range like 1-2.  You end up with min-2,3-max instead of
	 * just min-max.
	 */
	FOR_EACH_PTR(*list, tmp) {
		if (check_next) {
			/* Sometimes we overlap with more than one range
			   so we have to delete or modify the next range. */
			if (max + 1 == tmp->min) {
				/* join 2 ranges here */
				new->max = tmp->max;
				DELETE_CURRENT_PTR(tmp);
				return;
			}

			/* Doesn't overlap with the next one. */
			if (max < tmp->min)
				return;
			/* Partially overlaps with the next one. */
			if (max < tmp->max) {
				tmp->min = max + 1;
				return;
			}
			/* Completely overlaps with the next one. */
			if (max >= tmp->max) {
				DELETE_CURRENT_PTR(tmp);
				/* there could be more ranges to delete */
				continue;
			}
		}
		if (max != whole_range.max && max + 1 == tmp->min) {
			/* join 2 ranges into a big range */
			new = alloc_range(min, tmp->max);
			REPLACE_CURRENT_PTR(tmp, new);
			return;
		}
		if (max < tmp->min) {  /* new range entirely below */
			new = alloc_range(min, max);
			INSERT_CURRENT(new, tmp);
			return;
		}
		if (min < tmp->min) { /* new range partially below */
			if (max < tmp->max)
				max = tmp->max;
			else
				check_next = 1;
			new = alloc_range(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			if (!check_next)
				return;
			continue;
		}
		if (max <= tmp->max) /* new range already included */
			return;
		if (min <= tmp->max) { /* new range partially above */
			min = tmp->min;
			new = alloc_range(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			check_next = 1;
			continue;
		}
		if (min != whole_range.min && min - 1 == tmp->max) {
			/* join 2 ranges into a big range */
			new = alloc_range(tmp->min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			check_next = 1;
			continue;
		}
		/* the new range is entirely above the existing ranges */
	} END_FOR_EACH_PTR(tmp);
	if (check_next)
		return;
	new = alloc_range(min, max);
	add_ptr_list(list, new);
}

struct range_list *clone_range_list(struct range_list *list)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list *clone_permanent(struct range_list *list)
{
	struct data_range *tmp;
	struct data_range *new;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		new = alloc_range_perm(tmp->min, tmp->max);
		add_ptr_list(&ret, new);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list *range_list_union(struct range_list *one, struct range_list *two)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(one, tmp) {
		add_range(&ret, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);
	FOR_EACH_PTR(two, tmp) {
		add_range(&ret, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list *remove_range(struct range_list *list, long long min, long long max)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->max < min) {
			add_range(&ret, tmp->min, tmp->max);
			continue;
		}
		if (tmp->min > max) {
			add_range(&ret, tmp->min, tmp->max);
			continue;
		}
		if (tmp->min >= min && tmp->max <= max)
			continue;
		if (tmp->min >= min) {
			add_range(&ret, max + 1, tmp->max);
		} else if (tmp->max <= max) {
			add_range(&ret, tmp->min, min - 1);
		} else {
			add_range(&ret, tmp->min, min - 1);
			add_range(&ret, max + 1, tmp->max);
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list *invert_range_list(struct range_list *orig)
{
	struct range_list *ret = NULL;
	struct data_range *tmp;
	long long gap_min;

	if (!orig)
		return NULL;

	gap_min = whole_range.min;
	FOR_EACH_PTR(orig, tmp) {
		if (tmp->min > gap_min)
			add_range(&ret, gap_min, tmp->min - 1);
		gap_min = tmp->max + 1;
		if (tmp->max == whole_range.max)
			gap_min = whole_range.max;
	} END_FOR_EACH_PTR(tmp);

	if (gap_min != whole_range.max)
		add_range(&ret, gap_min, whole_range.max);

	return ret;
}

/*
 * if it can be only one and only value return 1, else return 0
 */
int estate_get_single_value(struct smatch_state *state, long long *val)
{
	struct data_info *dinfo;
	struct data_range *tmp;
	int count = 0;

	dinfo = get_dinfo(state);
	if (!dinfo || dinfo->type != DATA_RANGE)
		return 0;

	FOR_EACH_PTR(dinfo->value_ranges, tmp) {
		if (!count++) {
			if (tmp->min != tmp->max)
				return 0;
			*val = tmp->min;
		} else {
			return 0;
		}
	} END_FOR_EACH_PTR(tmp);
	return count;
}

int range_lists_equiv(struct range_list *one, struct range_list *two)
{
	struct data_range *one_range;
	struct data_range *two_range;

	PREPARE_PTR_LIST(one, one_range);
	PREPARE_PTR_LIST(two, two_range);
	for (;;) {
		if (!one_range && !two_range)
			return 1;
		if (!one_range || !two_range)
			return 0;
		if (one_range->min != two_range->min)
			return 0;
		if (one_range->max != two_range->max)
			return 0;
		NEXT_PTR_LIST(one_range);
		NEXT_PTR_LIST(two_range);
	}
	FINISH_PTR_LIST(two_range);
	FINISH_PTR_LIST(one_range);

	return 1;
}

int true_comparison_range(struct data_range *left, int comparison, struct data_range *right)
{
	switch (comparison) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left->min < right->max)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left->min <= right->max)
			return 1;
		return 0;
	case SPECIAL_EQUAL:
		if (left->max < right->min)
			return 0;
		if (left->min > right->max)
			return 0;
		return 1;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left->max >= right->min)
			return 1;
		return 0;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left->max > right->min)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left->min != left->max)
			return 1;
		if (right->min != right->max)
			return 1;
		if (left->min != right->min)
			return 1;
		return 0;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		return 0;
	}
	return 0;
}

int true_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left)
{
	if (left)
		return true_comparison_range(var, comparison, val);
	else
		return true_comparison_range(val, comparison, var);
}

static int false_comparison_range(struct data_range *left, int comparison, struct data_range *right)
{
	switch (comparison) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left->max >= right->min)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left->max > right->min)
			return 1;
		return 0;
	case SPECIAL_EQUAL:
		if (left->min != left->max)
			return 1;
		if (right->min != right->max)
			return 1;
		if (left->min != right->min)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left->min < right->max)
			return 1;
		return 0;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left->min <= right->max)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left->max < right->min)
			return 0;
		if (left->min > right->max)
			return 0;
		return 1;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		return 0;
	}
	return 0;
}

int false_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left)
{
	if (left)
		return false_comparison_range(var, comparison, val);
	else
		return false_comparison_range(val, comparison, var);
}

int possibly_true(struct expression *left, int comparison, struct expression *right)
{
	struct range_list *rl_left, *rl_right;
	struct data_range *tmp_left, *tmp_right;

	if (!get_implied_range_list(left, &rl_left))
		return 1;
	if (!get_implied_range_list(right, &rl_right))
		return 1;

	FOR_EACH_PTR(rl_left, tmp_left) {
		FOR_EACH_PTR(rl_right, tmp_right) {
			if (true_comparison_range(tmp_left, comparison, tmp_right))
				return 1;
		} END_FOR_EACH_PTR(tmp_right);
	} END_FOR_EACH_PTR(tmp_left);
	return 0;
}

int possibly_false(struct expression *left, int comparison, struct expression *right)
{
	struct range_list *rl_left, *rl_right;
	struct data_range *tmp_left, *tmp_right;

	if (!get_implied_range_list(left, &rl_left))
		return 1;
	if (!get_implied_range_list(right, &rl_right))
		return 1;

	FOR_EACH_PTR(rl_left, tmp_left) {
		FOR_EACH_PTR(rl_right, tmp_right) {
			if (false_comparison_range(tmp_left, comparison, tmp_right))
				return 1;
		} END_FOR_EACH_PTR(tmp_right);
	} END_FOR_EACH_PTR(tmp_left);
	return 0;
}

int possibly_true_range_lists(struct range_list *left_ranges, int comparison, struct range_list *right_ranges)
{
	struct data_range *left_tmp, *right_tmp;

	if (!left_ranges || !right_ranges)
		return 1;

	FOR_EACH_PTR(left_ranges, left_tmp) {
		FOR_EACH_PTR(right_ranges, right_tmp) {
			if (true_comparison_range(left_tmp, comparison, right_tmp))
				return 1;
		} END_FOR_EACH_PTR(right_tmp);
	} END_FOR_EACH_PTR(left_tmp);
	return 0;
}

int possibly_false_range_lists(struct range_list *left_ranges, int comparison, struct range_list *right_ranges)
{
	struct data_range *left_tmp, *right_tmp;

	if (!left_ranges || !right_ranges)
		return 1;

	FOR_EACH_PTR(left_ranges, left_tmp) {
		FOR_EACH_PTR(right_ranges, right_tmp) {
			if (false_comparison_range(left_tmp, comparison, right_tmp))
				return 1;
		} END_FOR_EACH_PTR(right_tmp);
	} END_FOR_EACH_PTR(left_tmp);
	return 0;
}

int possibly_true_range_lists_rl(int comparison, struct range_list *a, struct range_list *b, int left)
{
	if (left)
		return possibly_true_range_lists(a, comparison, b);
	else
		return possibly_true_range_lists(b, comparison, a);
}

int possibly_false_range_lists_rl(int comparison, struct range_list *a, struct range_list *b, int left)
{
	if (left)
		return possibly_false_range_lists(a, comparison, b);
	else
		return possibly_false_range_lists(b, comparison, a);
}

void tack_on(struct range_list **list, struct data_range *drange)
{
	add_ptr_list(list, drange);
}

int in_list_exact(struct range_list *list, struct data_range *drange)
{
	struct data_range *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->min == drange->min && tmp->max == drange->max)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

void push_range_list(struct range_list_stack **rl_stack, struct range_list *rl)
{
	add_ptr_list(rl_stack, rl);
}

struct range_list *pop_range_list(struct range_list_stack **rl_stack)
{
	struct range_list *rl;

	rl = last_ptr_list((struct ptr_list *)*rl_stack);
	delete_ptr_list_last((struct ptr_list **)rl_stack);
	return rl;
}

struct range_list *top_range_list(struct range_list_stack *rl_stack)
{
	struct range_list *rl;

	rl = last_ptr_list((struct ptr_list *)rl_stack);
	return rl;
}

void filter_top_range_list(struct range_list_stack **rl_stack, long long num)
{
	struct range_list *rl;

	rl = pop_range_list(rl_stack);
	rl = remove_range(rl, num, num);
	push_range_list(rl_stack, rl);
}

void free_range_list(struct range_list **rlist)
{
	__free_ptr_list((struct ptr_list **)rlist);
}

static void free_single_dinfo(struct data_info *dinfo)
{
	if (dinfo->type == DATA_RANGE)
		free_range_list(&dinfo->value_ranges);
}

static void free_dinfos(struct allocation_blob *blob)
{
	unsigned int size = sizeof(struct data_info);
	unsigned int offset = 0;

	while (offset < blob->offset) {
		free_single_dinfo((struct data_info *)(blob->data + offset));
		offset += size;
	}
}

void free_data_info_allocs(void)
{
	struct allocator_struct *desc = &data_info_allocator;
	struct allocation_blob *blob = desc->blobs;

	desc->blobs = NULL;
	desc->allocations = 0;
	desc->total_bytes = 0;
	desc->useful_bytes = 0;
	desc->freelist = NULL;
	while (blob) {
		struct allocation_blob *next = blob->next;
		free_dinfos(blob);
		blob_free(blob, desc->chunking);
		blob = next;
	}
	clear_data_range_alloc();
}

