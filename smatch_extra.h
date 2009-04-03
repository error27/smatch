/*
 * sparse/smatch_extra.h
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

enum data_type {
	DATA_NUM,
};

DECLARE_PTR_LIST(num_list, long long);

struct data_info {
	enum data_type type;
	int merged;
	struct num_list *values;
};
DECLARE_ALLOCATOR(data_info);

/* these are implimented in smatch_extra_helper.c */
struct data_info *alloc_data_info(long long num);
void add_num(struct num_list **list, long long num);
struct num_list *num_list_union(struct num_list *one, struct num_list *two);
int num_matches(struct data_info *dinfo, long long num);
long long get_single_value(struct data_info *dinfo);
int possibly_true(int comparison, struct data_info *dinfo, int num, int left);
int possibly_false(int comparison, struct data_info *dinfo, int num, int left);
