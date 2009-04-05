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

ALLOCATOR(data_info, "smatch extra data");
__DO_ALLOCATOR(long long, sizeof(long long), __alignof__(long long), "numbers", sm_num);

void print_num_list(struct num_list *list)
{
	long long *tmp;
	int i = 0;

	printf("(");
	FOR_EACH_PTR(list, tmp) {
		if (i++)
			printf(", ");
		printf("%lld", *tmp);
	} END_FOR_EACH_PTR(tmp);
	printf(")\n");
}

struct data_info *alloc_data_info(long long num)
{
	struct data_info *ret;

	ret = __alloc_data_info(0);
	ret->type = DATA_NUM;
	ret->merged = 0;
	ret->values = NULL;
	if (num != UNDEFINED)
		add_num(&ret->values, num);
	ret->filter = NULL;
	return ret;
}

void add_num(struct num_list **list, long long num)
{
 	long long *tmp;
 	long long *new;

 	FOR_EACH_PTR(*list, tmp) {
		if (*tmp < num)
			continue;
		else if (*tmp == num) {
			return;
		} else {
			new = __alloc_sm_num(0);
			*new = num;
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	new = __alloc_sm_num(0);
	*new = num;
	add_ptr_list(list, new);
}

struct num_list *clone_num_list(struct num_list *list)
{
	long long *tmp;
	struct num_list *ret = NULL;

	FOR_EACH_PTR(list, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct num_list *num_list_union(struct num_list *one, struct num_list *two)
{
	long long *tmp;
	struct num_list *ret = NULL;

	if (!one || !two)  /*having nothing in a list means everything is in */
		return NULL;

	FOR_EACH_PTR(one, tmp) {
		add_num(&ret, *tmp);
	} END_FOR_EACH_PTR(tmp);
	FOR_EACH_PTR(two, tmp) {
		add_num(&ret, *tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct num_list *num_list_intersection(struct num_list *one,
				       struct num_list *two)
{
	long long *one_val;
	long long *two_val;
	struct num_list *ret = NULL;

	PREPARE_PTR_LIST(one, one_val);
	PREPARE_PTR_LIST(two, two_val);
	for (;;) {
		if (!one_val || !two_val)
			break;
		if (*one_val < *two_val) {
			NEXT_PTR_LIST(one_val);
		} else if (*one_val == *two_val) {
			add_ptr_list(&ret, one_val);
			NEXT_PTR_LIST(one_val);
			NEXT_PTR_LIST(two_val);
		} else {
			NEXT_PTR_LIST(two_val);
		}
	}
	FINISH_PTR_LIST(two_val);
	FINISH_PTR_LIST(one_val);
	return ret;
}

static int num_in_list(struct num_list *list, long long num)
{
	long long *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (*tmp == num)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

int num_matches(struct data_info *dinfo, long long num)
{
	if (num_in_list(dinfo->values, num))
		return 1;
	return 0;
}

/* 
 * if it can be only one value return that, else return UNDEFINED
 */
long long get_single_value(struct data_info *dinfo)
{
	long long *tmp;
	int count = 0;
	long long ret = UNDEFINED;

	if (dinfo->type != DATA_NUM)
		return UNDEFINED;

	FOR_EACH_PTR(dinfo->values, tmp) {
		if (!count++)
			ret = *tmp;
		else
			return UNDEFINED;
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int possibly_true(int comparison, struct data_info *dinfo, int num, int left)
{
	long long *tmp;
	int ret = 0;

	if (comparison == SPECIAL_EQUAL && num_in_list(dinfo->filter, num))
		return 0;
	if (comparison == SPECIAL_NOTEQUAL && num_in_list(dinfo->filter, num))
		return 1;

	if (!dinfo->values)
		return 1;

	FOR_EACH_PTR(dinfo->values, tmp) {
		if (left)
			ret = true_comparison(*tmp, comparison, num);
		else
			ret = true_comparison(num,  comparison, *tmp);
		if (ret)
			return ret;
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

int possibly_false(int comparison, struct data_info *dinfo, int num, int left)
{
	long long *tmp;
	int ret = 0;

	if (comparison == SPECIAL_EQUAL && num_in_list(dinfo->filter, num))
		return 1;

	if (comparison == SPECIAL_NOTEQUAL && num_in_list(dinfo->filter, num))
		return 0;

	if (!dinfo->values)
		return 1;

	FOR_EACH_PTR(dinfo->values, tmp) {
		if (left)
			ret = !true_comparison(*tmp, comparison, num);
		else
			ret = !true_comparison(num,  comparison, *tmp);
		if (ret)
			return ret;
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static void free_single_dinfo(struct data_info *dinfo)
{
	__free_ptr_list((struct ptr_list **)&dinfo->values);
	__free_ptr_list((struct ptr_list **)&dinfo->filter);
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
	clear_sm_num_alloc();
}

