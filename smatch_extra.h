/*
 * sparse/smatch_extra.h
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

enum data_type {
	DATA_RANGE,
};

struct data_range {
	long long min;
	long long max;
};

DECLARE_PTR_LIST(range_list, struct data_range);

struct data_info {
	int merged;
	enum data_type type;
	struct range_list *value_ranges;
};
DECLARE_ALLOCATOR(data_info);

extern struct data_range whole_range;

/* these are implimented in smatch_extra_helper.c */
void add_range(struct range_list **list, long long min, long long max);
int possibly_true(int comparison, struct data_info *dinfo, int num, int left);
int possibly_false(int comparison, struct data_info *dinfo, int num, int left);
void free_data_info_allocs(void);
struct range_list *clone_range_list(struct range_list *list);
char *show_ranges(struct range_list *list);
struct range_list *remove_range(struct range_list *list, long long min, long long max);

/* used in smatch_slist.  implemented in smatch_extra.c */
struct sm_state *__extra_merge(struct sm_state *one, struct state_list *slist1,
			       struct sm_state *two, struct state_list *slist2);
struct sm_state *__extra_and_merge(struct sm_state *sm,
				     struct state_list_stack *stack);

/* also implemented in smatch_extra */
struct smatch_state *alloc_extra_state(int val);
struct smatch_state *add_filter(struct smatch_state *orig, long long filter);

struct data_info *alloc_dinfo_range(long long min, long long max);
struct range_list *range_list_union(struct range_list *one, struct range_list *two);
long long get_single_value_from_range(struct data_info *dinfo);
