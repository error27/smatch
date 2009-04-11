/*
 * sparse/smatch_extra_helper.c
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

static char *show_num(long long num)
{
	static char buff[256];

	if (num == whole_range.min) {
		snprintf(buff, 255, "min");
	} else if (num == whole_range.max) {
		snprintf(buff, 255, "max");
	} else if (num < 0) {
		snprintf(buff, 255, "(%lld)", num);
	} else {
		snprintf(buff, 255, "%lld", num);
	}
	buff[255] = '\0';
	return buff;
}

char *show_ranges(struct range_list *list)
{
	struct data_range *tmp;
	char full[256];
	int i = 0;

	full[0] = '\0';
 	FOR_EACH_PTR(list, tmp) {
		if (i++)
			strncat(full, ",", 254);
		if (tmp->min == tmp->max) {
			strncat(full, show_num(tmp->min), 254);
			continue;
		}
		strncat(full, show_num(tmp->min), 254);
		strncat(full, "-", 254);
		strncat(full, show_num(tmp->max), 254);
	} END_FOR_EACH_PTR(tmp);
	full[255] = '\0';
	return alloc_sname(full);
}

struct data_range *alloc_range(long long min, long long max)
{
	struct data_range *ret;

	if (min > max) {
		printf("Error invalid range %lld to %lld\n", min, max);
	}

	ret = __alloc_data_range(0);
	ret->min = min;
	ret->max = max;
	return ret;
}

struct data_info *alloc_dinfo_range(long long min, long long max)
{
	struct data_info *ret;

	ret = __alloc_data_info(0);
	ret->type = DATA_RANGE;
	ret->merged = 0;
	ret->value_ranges = NULL;
	add_range(&ret->value_ranges, min, max);
	return ret;
}

void add_range(struct range_list **list, long long min, long long max)
{
	struct data_range *tmp = NULL;
	struct data_range *new;

 	FOR_EACH_PTR(*list, tmp) {
		if (tmp->min < min) {
			continue;
		} else {
			if (tmp->max >= max)
				return;
			if (tmp->max >= min) {
				new = alloc_range(tmp->min, max);
				REPLACE_CURRENT_PTR(tmp, new);
				return;
			}
			new = alloc_range(min, max);
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
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

struct range_list *range_list_union(struct range_list *one, struct range_list *two)
{
	struct data_range *tmp;
	struct range_list *ret = NULL;

	if (!one || !two)  /*having nothing in a list means everything is in */
		return NULL;

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

/* 
 * if it can be only one value return that, else return UNDEFINED
 */
long long get_single_value_from_range(struct data_info *dinfo)
{
	struct data_range *tmp;
	int count = 0;
	long long ret = UNDEFINED;

	if (dinfo->type != DATA_RANGE)
		return UNDEFINED;

	FOR_EACH_PTR(dinfo->value_ranges, tmp) {
		if (!count++) {
			if (tmp->min != tmp->max)
				return UNDEFINED;
			ret = tmp->min;
		} else {
			return UNDEFINED;
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int possibly_true(int comparison, struct data_info *dinfo, int num, int left)
{
	struct data_range *tmp;
	int ret = 0;
	struct data_range drange;

	drange.min = num;
	drange.max = num;

	FOR_EACH_PTR(dinfo->value_ranges, tmp) {
		if (left)
			ret = true_comparison_range(tmp, comparison, &drange);
		else
			ret = true_comparison_range(&drange,  comparison, tmp);
		if (ret)
			return ret;
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int possibly_false(int comparison, struct data_info *dinfo, int num, int left)
{
	struct data_range *tmp;
	int ret = 0;
	struct data_range drange;

	drange.min = num;
	drange.max = num;

	FOR_EACH_PTR(dinfo->value_ranges, tmp) {
		if (left)
			ret = false_comparison_range(tmp, comparison, &drange);
		else
			ret = false_comparison_range(&drange,  comparison, tmp);
		if (ret)
			return ret;
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static void free_single_dinfo(struct data_info *dinfo)
{
	if (dinfo->type == DATA_RANGE)
		__free_ptr_list((struct ptr_list **)&dinfo->value_ranges);
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

