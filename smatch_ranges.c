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
ALLOCATOR(data_range_sval, "data range sval");
__DO_ALLOCATOR(struct data_range_sval, sizeof(struct data_range_sval), __alignof__(struct data_range_sval),
			 "permanent ranges sval", perm_data_range_sval);

char *show_ranges_sval(struct range_list_sval *list)
{
	struct data_range_sval *tmp;
	char full[256];
	int i = 0;

	full[0] = '\0';
	full[255] = '\0';
	FOR_EACH_PTR(list, tmp) {
		if (i++)
			strncat(full, ",", 254 - strlen(full));
		if (sval_cmp(tmp->min, tmp->max) == 0) {
			strncat(full, sval_to_str(tmp->min), 254 - strlen(full));
			continue;
		}
		strncat(full, sval_to_str(tmp->min), 254 - strlen(full));
		strncat(full, "-", 254 - strlen(full));
		strncat(full, sval_to_str(tmp->max), 254 - strlen(full));
	} END_FOR_EACH_PTR(tmp);
	return alloc_sname(full);
}

void get_value_ranges_sval(char *value, struct range_list_sval **rl)
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
			add_range_sval(rl, ll_to_sval(val1), ll_to_sval(val1));
			break;
		}
		if (*c == ',') {
			add_range_sval(rl, ll_to_sval(val1), ll_to_sval(val1));
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
		add_range_sval(rl, ll_to_sval(val1), ll_to_sval(val2));
		if (!*c)
			break;
		if (*c == ')')
			c++;
		c++; /* skip the comma in eg: 4-5,7 */
	}
}

int is_whole_range_rl_sval(struct range_list_sval *rl)
{
	struct data_range_sval *drange;

	if (ptr_list_empty(rl))
		return 1;
	drange = first_ptr_list((struct ptr_list *)rl);
	if (sval_is_min(drange->min) && sval_is_max(drange->max))
		return 1;
	return 0;
}

sval_t rl_min_sval(struct range_list_sval *rl)
{
	struct data_range_sval *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MIN;
	if (ptr_list_empty(rl))
		return ret;
	drange = first_ptr_list((struct ptr_list *)rl);
	return drange->min;
}

sval_t rl_max_sval(struct range_list_sval *rl)
{
	struct data_range_sval *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MAX;
	if (ptr_list_empty(rl))
		return ret;
	drange = last_ptr_list((struct ptr_list *)rl);
	return drange->max;
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

struct data_range_sval *alloc_range_sval(sval_t min, sval_t max)
{
	return alloc_range_helper_sval(min, max, 0);
}

struct data_range_sval *alloc_range_perm_sval(sval_t min, sval_t max)
{
	return alloc_range_helper_sval(min, max, 1);
}

struct range_list_sval *alloc_range_list_sval(sval_t min, sval_t max)
{
	struct range_list_sval *rl = NULL;

	add_range_sval(&rl, min, max);
	return rl;
}

struct range_list_sval *whole_range_list_sval(struct symbol *type)
{
	if (!type)
		type = &llong_ctype;

	return alloc_range_list_sval(sval_type_min(type), sval_type_max(type));
}

void add_range_sval(struct range_list_sval **list, sval_t min, sval_t max)
{
	struct data_range_sval *tmp = NULL;
	struct data_range_sval *new = NULL;
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
			if (max.value + 1 == tmp->min.value) {
				/* join 2 ranges here */
				new->max = tmp->max;
				DELETE_CURRENT_PTR(tmp);
				return;
			}

			/* Doesn't overlap with the next one. */
			if (sval_cmp(max, tmp->min) < 0)
				return;
			/* Partially overlaps with the next one. */
			if (sval_cmp(max, tmp->max) < 0) {
				tmp->min.value = max.value + 1;
				return;
			}
			/* Completely overlaps with the next one. */
			if (sval_cmp(max, tmp->max) >= 0) {
				DELETE_CURRENT_PTR(tmp);
				/* there could be more ranges to delete */
				continue;
			}
		}
		if (!sval_is_max(max) && max.value + 1 == tmp->min.value) {
			/* join 2 ranges into a big range */
			new = alloc_range_sval(min, tmp->max);
			REPLACE_CURRENT_PTR(tmp, new);
			return;
		}
		if (sval_cmp(max, tmp->min) < 0) { /* new range entirely below */
			new = alloc_range_sval(min, max);
			INSERT_CURRENT(new, tmp);
			return;
		}
		if (sval_cmp(min, tmp->min) < 0) { /* new range partially below */
			if (sval_cmp(max, tmp->max) < 0)
				max = tmp->max;
			else
				check_next = 1;
			new = alloc_range_sval(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			if (!check_next)
				return;
			continue;
		}
		if (sval_cmp(max, tmp->max) <= 0) /* new range already included */
			return;
		if (sval_cmp(min, tmp->max) <= 0) { /* new range partially above */
			min = tmp->min;
			new = alloc_range_sval(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			check_next = 1;
			continue;
		}
		if (!sval_is_min(min) && min.value - 1 == tmp->max.value) {
			/* join 2 ranges into a big range */
			new = alloc_range_sval(tmp->min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			check_next = 1;
			continue;
		}
		/* the new range is entirely above the existing ranges */
	} END_FOR_EACH_PTR(tmp);
	if (check_next)
		return;
	new = alloc_range_sval(min, max);
	add_ptr_list(list, new);
}

struct range_list_sval *clone_range_list_sval(struct range_list_sval *list)
{
	struct data_range_sval *tmp;
	struct range_list_sval *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list_sval *clone_permanent_sval(struct range_list_sval *list)
{
	struct data_range_sval *tmp;
	struct data_range_sval *new;
	struct range_list_sval *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		new = alloc_range_perm_sval(tmp->min, tmp->max);
		add_ptr_list(&ret, new);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list_sval *range_list_union_sval(struct range_list_sval *one, struct range_list_sval *two)
{
	struct data_range_sval *tmp;
	struct range_list_sval *ret = NULL;

	FOR_EACH_PTR(one, tmp) {
		add_range_sval(&ret, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);
	FOR_EACH_PTR(two, tmp) {
		add_range_sval(&ret, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list_sval *remove_range_sval(struct range_list_sval *list, sval_t min, sval_t max)
{
	struct data_range_sval *tmp;
	struct range_list_sval *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		if (sval_cmp(tmp->max, min) < 0) {
			add_range_sval(&ret, tmp->min, tmp->max);
			continue;
		}
		if (sval_cmp(tmp->min, max) > 0) {
			add_range_sval(&ret, tmp->min, tmp->max);
			continue;
		}
		if (sval_cmp(tmp->min, min) >= 0 && sval_cmp(tmp->max, max) <= 0)
			continue;
		if (sval_cmp(tmp->min, min) >= 0) {
			max.value++;
			add_range_sval(&ret, max, tmp->max);
		} else if (sval_cmp(tmp->max, max) <= 0) {
			min.value--;
			add_range_sval(&ret, tmp->min, min);
		} else {
			min.value--;
			max.value++;
			add_range_sval(&ret, tmp->min, min);
			add_range_sval(&ret, max, tmp->max);
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int ranges_equiv_sval(struct data_range_sval *one, struct data_range_sval *two)
{
	if (!one && !two)
		return 1;
	if (!one || !two)
		return 0;
	if (sval_cmp(one->min, two->min) != 0)
		return 0;
	if (sval_cmp(one->max, two->max) != 0)
		return 0;
	return 1;
}

int range_lists_equiv_sval(struct range_list_sval *one, struct range_list_sval *two)
{
	struct data_range_sval *one_range;
	struct data_range_sval *two_range;

	PREPARE_PTR_LIST(one, one_range);
	PREPARE_PTR_LIST(two, two_range);
	for (;;) {
		if (!one_range && !two_range)
			return 1;
		if (!ranges_equiv_sval(one_range, two_range))
			return 0;
		NEXT_PTR_LIST(one_range);
		NEXT_PTR_LIST(two_range);
	}
	FINISH_PTR_LIST(two_range);
	FINISH_PTR_LIST(one_range);

	return 1;
}

int true_comparison_range_sval(struct data_range_sval *left, int comparison, struct data_range_sval *right)
{
	switch (comparison) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (sval_cmp(left->min, right->max) < 0)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (sval_cmp(left->min, right->max) <= 0)
			return 1;
		return 0;
	case SPECIAL_EQUAL:
		if (sval_cmp(left->max, right->min) < 0)
			return 0;
		if (sval_cmp(left->min, right->max) > 0)
			return 0;
		return 1;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (sval_cmp(left->max, right->min) >= 0)
			return 1;
		return 0;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (sval_cmp(left->max, right->min) > 0)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (sval_cmp(left->min, left->max) != 0)
			return 1;
		if (sval_cmp(right->min, right->max) != 0)
			return 1;
		if (sval_cmp(left->min, right->min) != 0)
			return 1;
		return 0;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		return 0;
	}
	return 0;
}

int true_comparison_range_lr_sval(int comparison, struct data_range_sval *var, struct data_range_sval *val, int left)
{
	if (left)
		return true_comparison_range_sval(var, comparison, val);
	else
		return true_comparison_range_sval(val, comparison, var);
}

static int false_comparison_range_sval(struct data_range_sval *left, int comparison, struct data_range_sval *right)
{
	switch (comparison) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (sval_cmp(left->max, right->min) >= 0)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (sval_cmp(left->max, right->min) > 0)
			return 1;
		return 0;
	case SPECIAL_EQUAL:
		if (sval_cmp(left->min, left->max) != 0)
			return 1;
		if (sval_cmp(right->min, right->max) != 0)
			return 1;
		if (sval_cmp(left->min, right->min) != 0)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (sval_cmp(left->min, right->max) < 0)
			return 1;
		return 0;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (sval_cmp(left->min, right->max) <= 0)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (sval_cmp(left->max, right->min) < 0)
			return 0;
		if (sval_cmp(left->min, right->max) > 0)
			return 0;
		return 1;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		return 0;
	}
	return 0;
}

int false_comparison_range_lr_sval(int comparison, struct data_range_sval *var, struct data_range_sval *val, int left)
{
	if (left)
		return false_comparison_range_sval(var, comparison, val);
	else
		return false_comparison_range_sval(val, comparison, var);
}

int possibly_true(struct expression *left, int comparison, struct expression *right)
{
	struct range_list_sval *rl_left, *rl_right;
	struct data_range_sval *tmp_left, *tmp_right;

	if (!get_implied_range_list_sval(left, &rl_left))
		return 1;
	if (!get_implied_range_list_sval(right, &rl_right))
		return 1;

	FOR_EACH_PTR(rl_left, tmp_left) {
		FOR_EACH_PTR(rl_right, tmp_right) {
			if (true_comparison_range_sval(tmp_left, comparison, tmp_right))
				return 1;
		} END_FOR_EACH_PTR(tmp_right);
	} END_FOR_EACH_PTR(tmp_left);
	return 0;
}

int possibly_false(struct expression *left, int comparison, struct expression *right)
{
	struct range_list_sval *rl_left, *rl_right;
	struct data_range_sval *tmp_left, *tmp_right;

	if (!get_implied_range_list_sval(left, &rl_left))
		return 1;
	if (!get_implied_range_list_sval(right, &rl_right))
		return 1;

	FOR_EACH_PTR(rl_left, tmp_left) {
		FOR_EACH_PTR(rl_right, tmp_right) {
			if (false_comparison_range_sval(tmp_left, comparison, tmp_right))
				return 1;
		} END_FOR_EACH_PTR(tmp_right);
	} END_FOR_EACH_PTR(tmp_left);
	return 0;
}

int possibly_true_range_lists_sval(struct range_list_sval *left_ranges, int comparison, struct range_list_sval *right_ranges)
{
	struct data_range_sval *left_tmp, *right_tmp;

	if (!left_ranges || !right_ranges)
		return 1;

	FOR_EACH_PTR(left_ranges, left_tmp) {
		FOR_EACH_PTR(right_ranges, right_tmp) {
			if (true_comparison_range_sval(left_tmp, comparison, right_tmp))
				return 1;
		} END_FOR_EACH_PTR(right_tmp);
	} END_FOR_EACH_PTR(left_tmp);
	return 0;
}

int possibly_false_range_lists_sval(struct range_list_sval *left_ranges, int comparison, struct range_list_sval *right_ranges)
{
	struct data_range_sval *left_tmp, *right_tmp;

	if (!left_ranges || !right_ranges)
		return 1;

	FOR_EACH_PTR(left_ranges, left_tmp) {
		FOR_EACH_PTR(right_ranges, right_tmp) {
			if (false_comparison_range_sval(left_tmp, comparison, right_tmp))
				return 1;
		} END_FOR_EACH_PTR(right_tmp);
	} END_FOR_EACH_PTR(left_tmp);
	return 0;
}

/* FIXME: the _rl here stands for right left so really it should be _lr */
int possibly_true_range_lists_rl_sval(int comparison, struct range_list_sval *a, struct range_list_sval *b, int left)
{
	if (left)
		return possibly_true_range_lists_sval(a, comparison, b);
	else
		return possibly_true_range_lists_sval(b, comparison, a);
}

int possibly_false_range_lists_rl_sval(int comparison, struct range_list_sval *a, struct range_list_sval *b, int left)
{
	if (left)
		return possibly_false_range_lists_sval(a, comparison, b);
	else
		return possibly_false_range_lists_sval(b, comparison, a);
}

void tack_on_sval(struct range_list_sval **list, struct data_range_sval *drange)
{
	add_ptr_list(list, drange);
}

void push_range_list_sval(struct range_list_stack_sval **rl_stack, struct range_list_sval *rl)
{
	add_ptr_list(rl_stack, rl);
}

struct range_list_sval *pop_range_list_sval(struct range_list_stack_sval **rl_stack)
{
	struct range_list_sval *rl;

	rl = last_ptr_list((struct ptr_list *)*rl_stack);
	delete_ptr_list_last((struct ptr_list **)rl_stack);
	return rl;
}

struct range_list_sval *top_range_list_sval(struct range_list_stack_sval *rl_stack)
{
	struct range_list_sval *rl;

	rl = last_ptr_list((struct ptr_list *)rl_stack);
	return rl;
}

void filter_top_range_list_sval(struct range_list_stack_sval **rl_stack, sval_t sval)
{
	struct range_list_sval *rl;

	rl = pop_range_list_sval(rl_stack);
	rl = remove_range_sval(rl, sval, sval);
	push_range_list_sval(rl_stack, rl);
}

struct range_list_sval *cast_rl(struct range_list_sval *rl, struct symbol *type)
{
	struct data_range_sval *tmp;
	struct data_range_sval *new;
	struct range_list_sval *ret = NULL;
	int set_min, set_max;

	if (!rl)
		return NULL;

	if (!type)
		return clone_range_list_sval(rl);

	if (sval_cmp(rl_min_sval(rl), rl_max_sval(rl)) == 0) {
		sval_t sval = sval_cast(rl_min_sval(rl), type);
		return alloc_range_list_sval(sval, sval);
	}

	set_max = 0;
	if (type_unsigned(type) && sval_cmp_val(rl_min_sval(rl), 0) < 0)
		set_max = 1;

	set_min = 0;
	if (type_signed(type) && sval_cmp(rl_max_sval(rl), sval_type_max(type)) > 0)
		set_min = 1;

	FOR_EACH_PTR(rl, tmp) {
		sval_t min, max;

		min = tmp->min;
		max = tmp->max;

		if (sval_cmp_t(type, max, sval_type_min(type)) < 0)
			continue;
		if (sval_cmp_t(type, min, sval_type_max(type)) > 0)
			continue;
		if (sval_cmp_val(min, 0) < 0 && type_unsigned(type))
			min.value = 0;
		new = alloc_range_sval(sval_cast(min, type), sval_cast(max, type));
		add_ptr_list(&ret, new);
	} END_FOR_EACH_PTR(tmp);

	if (!ret)
		return whole_range_list_sval(type);

	if (set_min) {
		tmp = first_ptr_list((struct ptr_list *)ret);
		tmp->min = sval_type_min(type);
	}
	if (set_max) {
		tmp = last_ptr_list((struct ptr_list *)ret);
		tmp->max = sval_type_max(type);
	}

	return ret;
}

void free_range_list_sval(struct range_list_sval **rlist)
{
	__free_ptr_list((struct ptr_list **)rlist);
}

static void free_single_dinfo(struct data_info *dinfo)
{
	if (dinfo->type == DATA_RANGE)
		free_range_list_sval(&dinfo->value_ranges);
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
	clear_data_range_sval_alloc();
}

