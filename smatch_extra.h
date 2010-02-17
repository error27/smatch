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

DECLARE_PTR_LIST(range_list, struct data_range);
DECLARE_PTR_LIST(range_list_stack, struct range_list);

struct data_info {
	enum data_type type;
	struct range_list *value_ranges;
};
DECLARE_ALLOCATOR(data_info);

/* these are implimented in smatch_ranges.c */
struct data_range *alloc_range_perm(long long min, long long max);
void add_range(struct range_list **list, long long min, long long max);
int true_comparison_range(struct data_range *left, int comparison, struct data_range *right);
int possibly_true(int comparison, struct data_info *dinfo, long long num, int left);
int possibly_true_range_list(int comparison, struct data_info *dinfo, struct range_list *values, int left);
int possibly_false(int comparison, struct data_info *dinfo, long long num, int left);
int possibly_false_range_list(int comparison, struct data_info *dinfo, struct range_list *values, int left);
void free_data_info_allocs(void);
struct range_list *clone_range_list(struct range_list *list);
char *show_ranges(struct range_list *list);
struct range_list *remove_range(struct range_list *list, long long min, long long max);

/* used in smatch_slist.  implemented in smatch_extra.c */
int implied_not_equal(struct expression *expr, long long val);
struct sm_state *__extra_pre_loop_hook_before(struct statement *iterator_pre_statement);
int __iterator_unchanged(struct sm_state *sm, struct statement *iterator);
void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition);

/* also implemented in smatch_extra */
struct smatch_state *alloc_extra_state(long long val);
struct smatch_state *alloc_extra_state_range_list(struct range_list *rl);
struct data_info *get_dinfo(struct smatch_state *state);
struct smatch_state *add_filter(struct smatch_state *orig, long long filter);
struct smatch_state *extra_undefined(void);

struct data_info *alloc_dinfo_range(long long min, long long max);
struct data_info *alloc_dinfo_range_list(struct range_list *rl);
struct range_list *range_list_union(struct range_list *one, struct range_list *two);
long long get_dinfo_min(struct data_info *dinfo);
long long get_dinfo_max(struct data_info *dinfo);
int get_single_value_from_range(struct data_info *dinfo, long long *val);

void function_comparison(int comparison, struct expression *expr, long long value, int left);

int true_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
int false_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
struct data_range *alloc_range(long long min, long long max);
void tack_on(struct range_list **list, struct data_range *drange);
int in_list_exact(struct range_list *list, struct data_range *drange);

struct smatch_state *alloc_extra_state_range(long long min, long long max);

void push_range_list(struct range_list_stack **rl_stack, struct range_list *rl);
struct range_list *pop_range_list(struct range_list_stack **rl_stack);
struct range_list *top_range_list(struct range_list_stack *rl_stack);
void filter_top_range_list(struct range_list_stack **rl_stack, long long num);
int get_implied_range_list(struct expression *expr, struct range_list **rl);
