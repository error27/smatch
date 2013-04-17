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

char *show_rl(struct range_list *list)
{
	struct data_range *tmp;
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

static sval_t parse_val(struct symbol *type, char *c, char **endp)
{
	char *start = c;
	sval_t ret;

	if (!strncmp(start, "max", 3)) {
		ret = sval_type_max(type);
		c += 3;
	} else if (!strncmp(start, "u64max", 6)) {
		ret = sval_type_val(type, ULLONG_MAX);
		c += 6;
	} else if (!strncmp(start, "s64max", 6)) {
		ret = sval_type_val(type, LLONG_MAX);
		c += 6;
	} else if (!strncmp(start, "u32max", 6)) {
		ret = sval_type_val(type, UINT_MAX);
		c += 6;
	} else if (!strncmp(start, "s32max", 6)) {
		ret = sval_type_val(type, INT_MAX);
		c += 6;
	} else if (!strncmp(start, "u16max", 6)) {
		ret = sval_type_val(type, USHRT_MAX);
		c += 6;
	} else if (!strncmp(start, "s16max", 6)) {
		ret = sval_type_val(type, SHRT_MAX);
		c += 6;
	} else if (!strncmp(start, "min", 3)) {
		ret = sval_type_min(type);
		c += 3;
	} else if (!strncmp(start, "s64min", 6)) {
		ret = sval_type_val(type, LLONG_MIN);
		c += 6;
	} else if (!strncmp(start, "s32min", 6)) {
		ret = sval_type_val(type, INT_MIN);
		c += 6;
	} else if (!strncmp(start, "s16min", 6)) {
		ret = sval_type_val(type, SHRT_MIN);
		c += 6;
	} else {
		ret = sval_type_val(type, strtoll(start, &c, 10));
	}
	*endp = c;
	return ret;
}

void str_to_rl(struct symbol *type, char *value, struct range_list **rl)
{
	sval_t min, max;
	char *c;

	if (!type)
		type = &llong_ctype;
	*rl = NULL;

	if (value && strcmp(value, "empty") == 0)
		return;

	c = value;
	while (*c) {
		if (*c == '(')
			c++;
		min = parse_val(type, c, &c);
		if (*c == ')')
			c++;
		if (!*c) {
			add_range(rl, min, min);
			break;
		}
		if (*c == ',') {
			add_range(rl, min, min);
			c++;
			continue;
		}
		if (*c != '-') {
			sm_msg("debug XXX: trouble parsing %s ", value);
			break;
		}
		c++;
		if (*c == '(')
			c++;
		max = parse_val(type, c, &c);
		add_range(rl, min, max);
		if (*c == ')')
			c++;
		if (!*c)
			break;
		if (*c != ',') {
			sm_msg("debug YYY: trouble parsing %s %s", value, c);
			break;
		}
		c++;
	}

	*rl = cast_rl(type, *rl);
}

int is_whole_rl(struct range_list *rl)
{
	struct data_range *drange;

	if (ptr_list_empty(rl))
		return 0;
	drange = first_ptr_list((struct ptr_list *)rl);
	if (sval_is_min(drange->min) && sval_is_max(drange->max))
		return 1;
	return 0;
}

sval_t rl_min(struct range_list *rl)
{
	struct data_range *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MIN;
	if (ptr_list_empty(rl))
		return ret;
	drange = first_ptr_list((struct ptr_list *)rl);
	return drange->min;
}

sval_t rl_max(struct range_list *rl)
{
	struct data_range *drange;
	sval_t ret;

	ret.type = &llong_ctype;
	ret.value = LLONG_MAX;
	if (ptr_list_empty(rl))
		return ret;
	drange = last_ptr_list((struct ptr_list *)rl);
	return drange->max;
}

struct symbol *rl_type(struct range_list *rl)
{
	if (!rl)
		return NULL;
	return rl_min(rl).type;
}

static struct data_range *alloc_range_helper_sval(sval_t min, sval_t max, int perm)
{
	struct data_range *ret;

	if (perm)
		ret = __alloc_perm_data_range(0);
	else
		ret = __alloc_data_range(0);
	ret->min = min;
	ret->max = max;
	return ret;
}

struct data_range *alloc_range(sval_t min, sval_t max)
{
	return alloc_range_helper_sval(min, max, 0);
}

struct data_range *alloc_range_perm(sval_t min, sval_t max)
{
	return alloc_range_helper_sval(min, max, 1);
}

struct range_list *alloc_rl(sval_t min, sval_t max)
{
	struct range_list *rl = NULL;

	if (sval_cmp(min, max) > 0)
		return alloc_whole_rl(min.type);

	add_range(&rl, min, max);
	return rl;
}

struct range_list *alloc_whole_rl(struct symbol *type)
{
	if (!type || type_positive_bits(type) < 0)
		type = &llong_ctype;

	return alloc_rl(sval_type_min(type), sval_type_max(type));
}

void add_range(struct range_list **list, sval_t min, sval_t max)
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
			new = alloc_range(min, tmp->max);
			REPLACE_CURRENT_PTR(tmp, new);
			return;
		}
		if (sval_cmp(max, tmp->min) < 0) { /* new range entirely below */
			new = alloc_range(min, max);
			INSERT_CURRENT(new, tmp);
			return;
		}
		if (sval_cmp(min, tmp->min) < 0) { /* new range partially below */
			if (sval_cmp(max, tmp->max) < 0)
				max = tmp->max;
			else
				check_next = 1;
			new = alloc_range(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			if (!check_next)
				return;
			continue;
		}
		if (sval_cmp(max, tmp->max) <= 0) /* new range already included */
			return;
		if (sval_cmp(min, tmp->max) <= 0) { /* new range partially above */
			min = tmp->min;
			new = alloc_range(min, max);
			REPLACE_CURRENT_PTR(tmp, new);
			check_next = 1;
			continue;
		}
		if (!sval_is_min(min) && min.value - 1 == tmp->max.value) {
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

struct range_list *clone_rl(struct range_list *list)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct range_list *clone_rl_permanent(struct range_list *list)
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

struct range_list *rl_union(struct range_list *one, struct range_list *two)
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

struct range_list *remove_range(struct range_list *list, sval_t min, sval_t max)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		if (sval_cmp(tmp->max, min) < 0) {
			add_range(&ret, tmp->min, tmp->max);
			continue;
		}
		if (sval_cmp(tmp->min, max) > 0) {
			add_range(&ret, tmp->min, tmp->max);
			continue;
		}
		if (sval_cmp(tmp->min, min) >= 0 && sval_cmp(tmp->max, max) <= 0)
			continue;
		if (sval_cmp(tmp->min, min) >= 0) {
			max.value++;
			add_range(&ret, max, tmp->max);
		} else if (sval_cmp(tmp->max, max) <= 0) {
			min.value--;
			add_range(&ret, tmp->min, min);
		} else {
			min.value--;
			max.value++;
			add_range(&ret, tmp->min, min);
			add_range(&ret, max, tmp->max);
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int ranges_equiv(struct data_range *one, struct data_range *two)
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

int rl_equiv(struct range_list *one, struct range_list *two)
{
	struct data_range *one_range;
	struct data_range *two_range;

	if (one == two)
		return 1;

	PREPARE_PTR_LIST(one, one_range);
	PREPARE_PTR_LIST(two, two_range);
	for (;;) {
		if (!one_range && !two_range)
			return 1;
		if (!ranges_equiv(one_range, two_range))
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

int true_comparison_range_LR(int comparison, struct data_range *var, struct data_range *val, int left)
{
	if (left)
		return true_comparison_range(var, comparison, val);
	else
		return true_comparison_range(val, comparison, var);
}

static int false_comparison_range_sval(struct data_range *left, int comparison, struct data_range *right)
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

int false_comparison_range_LR(int comparison, struct data_range *var, struct data_range *val, int left)
{
	if (left)
		return false_comparison_range_sval(var, comparison, val);
	else
		return false_comparison_range_sval(val, comparison, var);
}

int possibly_true(struct expression *left, int comparison, struct expression *right)
{
	struct range_list *rl_left, *rl_right;
	struct data_range *tmp_left, *tmp_right;

	if (!get_implied_rl(left, &rl_left))
		return 1;
	if (!get_implied_rl(right, &rl_right))
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

	if (!get_implied_rl(left, &rl_left))
		return 1;
	if (!get_implied_rl(right, &rl_right))
		return 1;

	FOR_EACH_PTR(rl_left, tmp_left) {
		FOR_EACH_PTR(rl_right, tmp_right) {
			if (false_comparison_range_sval(tmp_left, comparison, tmp_right))
				return 1;
		} END_FOR_EACH_PTR(tmp_right);
	} END_FOR_EACH_PTR(tmp_left);
	return 0;
}

int possibly_true_rl(struct range_list *left_ranges, int comparison, struct range_list *right_ranges)
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

int possibly_false_rl(struct range_list *left_ranges, int comparison, struct range_list *right_ranges)
{
	struct data_range *left_tmp, *right_tmp;

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
int possibly_true_rl_LR(int comparison, struct range_list *a, struct range_list *b, int left)
{
	if (left)
		return possibly_true_rl(a, comparison, b);
	else
		return possibly_true_rl(b, comparison, a);
}

int possibly_false_rl_LR(int comparison, struct range_list *a, struct range_list *b, int left)
{
	if (left)
		return possibly_false_rl(a, comparison, b);
	else
		return possibly_false_rl(b, comparison, a);
}

void tack_on(struct range_list **list, struct data_range *drange)
{
	add_ptr_list(list, drange);
}

void push_rl(struct range_list_stack **rl_stack, struct range_list *rl)
{
	add_ptr_list(rl_stack, rl);
}

struct range_list *pop_rl(struct range_list_stack **rl_stack)
{
	struct range_list *rl;

	rl = last_ptr_list((struct ptr_list *)*rl_stack);
	delete_ptr_list_last((struct ptr_list **)rl_stack);
	return rl;
}

struct range_list *top_rl(struct range_list_stack *rl_stack)
{
	struct range_list *rl;

	rl = last_ptr_list((struct ptr_list *)rl_stack);
	return rl;
}

void filter_top_rl(struct range_list_stack **rl_stack, sval_t sval)
{
	struct range_list *rl;

	rl = pop_rl(rl_stack);
	rl = remove_range(rl, sval, sval);
	push_rl(rl_stack, rl);
}

static int sval_too_big(struct symbol *type, sval_t sval)
{
	if (type_bits(type) == 64)
		return 0;
	if (sval.uvalue > ((1ULL << type_bits(type)) - 1))
		return 1;
	return 0;
}

static void add_range_t(struct symbol *type, struct range_list **rl, sval_t min, sval_t max)
{
	/* If we're just adding a number, cast it and add it */
	if (sval_cmp(min, max) == 0) {
		add_range(rl, sval_cast(type, min), sval_cast(type, max));
		return;
	}

	/* If the range is within the type range then add it */
	if (sval_fits(type, min) && sval_fits(type, max)) {
		add_range(rl, sval_cast(type, min), sval_cast(type, max));
		return;
	}

	/*
	 * If the range we are adding has more bits than the range type then
	 * add the whole range type.  Eg:
	 * 0x8000000000000000 - 0xf000000000000000 -> cast to int
	 * This isn't totally the right thing to do.  We could be more granular.
	 */
	if (sval_too_big(type, min) || sval_too_big(type, max)) {
		add_range(rl, sval_type_min(type), sval_type_max(type));
		return;
	}

	/* Cast negative values to high positive values */
	if (sval_is_negative(min) && type_unsigned(type)) {
		if (sval_is_positive(max)) {
			if (sval_too_high(type, max)) {
				add_range(rl, sval_type_min(type), sval_type_max(type));
				return;
			}
			add_range(rl, sval_type_val(type, 0), sval_cast(type, max));
			max = sval_type_max(type);
		} else {
			max = sval_cast(type, max);
		}
		min = sval_cast(type, min);
		add_range(rl, min, max);
	}

	/* Cast high positive numbers to negative */
	if (sval_unsigned(max) && sval_is_negative(sval_cast(type, max))) {
		if (!sval_is_negative(sval_cast(type, min))) {
			add_range(rl, sval_cast(type, min), sval_type_max(type));
			min = sval_type_min(type);
		} else {
			min = sval_cast(type, min);
		}
		max = sval_cast(type, max);
		add_range(rl, min, max);
	}

	return;
}

struct range_list *rl_truncate_cast(struct symbol *type, struct range_list *rl)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;
	sval_t min, max;

	if (!rl)
		return NULL;

	if (!type || type == rl_type(rl))
		return rl;

	FOR_EACH_PTR(rl, tmp) {
		min = tmp->min;
		max = tmp->max;
		if (type_bits(type) < type_bits(rl_type(rl))) {
			min.uvalue = tmp->min.uvalue & ((1ULL << type_bits(type)) - 1);
			max.uvalue = tmp->max.uvalue & ((1ULL << type_bits(type)) - 1);
		}
		if (sval_cmp(min, max) > 0) {
			min = sval_cast(type, min);
			max = sval_cast(type, max);
		}
		add_range_t(type, &ret, min, max);
	} END_FOR_EACH_PTR(tmp);

	return ret;
}

static int rl_is_sane(struct range_list *rl)
{
	struct data_range *tmp;
	struct symbol *type;

	type = rl_type(rl);
	FOR_EACH_PTR(rl, tmp) {
		if (!sval_fits(type, tmp->min))
			return 0;
		if (!sval_fits(type, tmp->max))
			return 0;
		if (sval_cmp(tmp->min, tmp->max) > 0)
			return 0;
	} END_FOR_EACH_PTR(tmp);

	return 1;
}

static int rl_type_consistent(struct range_list *rl)
{
	struct data_range *tmp;
	struct symbol *type;

	type = rl_type(rl);
	FOR_EACH_PTR(rl, tmp) {
		if (type != tmp->min.type || type != tmp->max.type)
			return 0;
	} END_FOR_EACH_PTR(tmp);
	return 1;
}

struct range_list *cast_rl(struct symbol *type, struct range_list *rl)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	if (!rl)
		return NULL;

	if (!type)
		return rl;
	if (!rl_is_sane(rl))
		return alloc_whole_rl(type);
	if (type == rl_type(rl) && rl_type_consistent(rl))
		return rl;

	FOR_EACH_PTR(rl, tmp) {
		add_range_t(type, &ret, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);

	if (!ret)
		return alloc_whole_rl(type);

	return ret;
}

struct range_list *rl_invert(struct range_list *orig)
{
	struct range_list *ret = NULL;
	struct data_range *tmp;
	sval_t gap_min, abs_max, sval;

	if (!orig)
		return NULL;

	gap_min = sval_type_min(rl_min(orig).type);
	abs_max = sval_type_max(rl_max(orig).type);

	FOR_EACH_PTR(orig, tmp) {
		if (sval_cmp(tmp->min, gap_min) > 0) {
			sval = sval_type_val(tmp->min.type, tmp->min.value - 1);
			add_range(&ret, gap_min, sval);
		}
		gap_min = sval_type_val(tmp->max.type, tmp->max.value + 1);
		if (sval_cmp(tmp->max, abs_max) == 0)
			gap_min = abs_max;
	} END_FOR_EACH_PTR(tmp);

	if (sval_cmp(gap_min, abs_max) < 0)
		add_range(&ret, gap_min, abs_max);

	return ret;
}

struct range_list *rl_filter(struct range_list *rl, struct range_list *filter)
{
	struct data_range *tmp;

	FOR_EACH_PTR(filter, tmp) {
		rl = remove_range(rl, tmp->min, tmp->max);
	} END_FOR_EACH_PTR(tmp);

	return rl;
}

struct range_list *rl_intersection(struct range_list *one, struct range_list *two)
{
	if (!two)
		return NULL;
	two = rl_invert(two);
	return rl_filter(one, two);
}

void free_rl(struct range_list **rlist)
{
	__free_ptr_list((struct ptr_list **)rlist);
}

static void free_single_dinfo(struct data_info *dinfo)
{
	free_rl(&dinfo->value_ranges);
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

