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

struct relation {
	int op;
	char *name;
	struct symbol *sym;
};

DECLARE_PTR_LIST(related_list, struct relation);

struct data_info {
	struct related_list *related;
	enum data_type type;
	struct range_list *value_ranges;
};
DECLARE_ALLOCATOR(data_info);

extern struct string_list *__ignored_macros;

/* these are implimented in smatch_ranges.c */
int is_whole_range_rl(struct range_list *rl);
long long rl_min(struct range_list *rl);
long long rl_max(struct range_list *rl);
struct data_range *alloc_range_perm(long long min, long long max);
struct range_list *alloc_range_list(long long min, long long max);
void add_range(struct range_list **list, long long min, long long max);
int true_comparison_range(struct data_range *left, int comparison, struct data_range *right);
int possibly_true(int comparison, struct expression *expr, long long num, int left);
int possibly_true_range_lists(struct range_list *left_ranges, int comparison, struct range_list *right_ranges);
int possibly_true_range_list_lr(int comparison, struct smatch_state *estate, struct range_list *values, int left);
int possibly_false(int comparison, struct expression *expr, long long num, int left);
int possibly_false_range_lists(struct range_list *left_ranges, int comparison, struct range_list *right_ranges);
int possibly_false_range_list_lr(int comparison, struct smatch_state *estate, struct range_list *values, int left);
void free_range_list(struct range_list **rlist);
void free_data_info_allocs(void);
struct range_list *clone_range_list(struct range_list *list);
char *show_ranges(struct range_list *list);
void get_value_ranges(char *value, struct range_list **rl);

struct range_list *remove_range(struct range_list *list, long long min, long long max);

/* used in smatch_slist.  implemented in smatch_extra.c */
int implied_not_equal(struct expression *expr, long long val);
struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct state_list **slist);
int __iterator_unchanged(struct sm_state *sm);
void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition);

/* also implemented in smatch_extra */
struct smatch_state *clone_estate(struct smatch_state *state);
struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state);
struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state);
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state);
struct smatch_state *alloc_estate_empty(void);
struct smatch_state *alloc_estate(long long val);
struct smatch_state *alloc_estate_range_list(struct range_list *rl);
struct range_list *get_range_list(struct expression *expr);
struct data_info *get_dinfo(struct smatch_state *state);
struct range_list *estate_ranges(struct smatch_state *state);
struct related_list *estate_related(struct smatch_state *state);
long long estate_min(struct smatch_state *state);
long long estate_max(struct smatch_state *state);
struct smatch_state *add_filter(struct smatch_state *orig, long long filter);
struct smatch_state *extra_undefined(void);

struct range_list *range_list_union(struct range_list *one, struct range_list *two);
int estate_get_single_value(struct smatch_state *estate, long long *val);

void function_comparison(int comparison, struct expression *expr, long long value, int left);

int true_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
int false_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
struct data_range *alloc_range(long long min, long long max);
void tack_on(struct range_list **list, struct data_range *drange);
int in_list_exact(struct range_list *list, struct data_range *drange);

struct smatch_state *alloc_estate_range(long long min, long long max);

void push_range_list(struct range_list_stack **rl_stack, struct range_list *rl);
struct range_list *pop_range_list(struct range_list_stack **rl_stack);
struct range_list *top_range_list(struct range_list_stack *rl_stack);
void filter_top_range_list(struct range_list_stack **rl_stack, long long num);
int get_implied_range_list(struct expression *expr, struct range_list **rl);
int is_whole_range(struct smatch_state *state);

/* implemented in smatch_constraints */
void set_equiv(struct sm_state *right_sm, struct expression *left);
struct relation *get_common_relationship(struct smatch_state *estate, int op,
					const char *name, struct symbol *sym);
struct related_list *clone_related_list(struct related_list *related);
void add_related(struct smatch_state *state, int op, const char *name, struct symbol *sym);
void add_equiv(struct smatch_state *state, const char *name, struct symbol *sym);
void remove_from_equiv(const char *name, struct symbol *sym);
void remove_from_equiv_expr(struct expression *expr);
void set_equiv_state_expr(int id, struct expression *expr, struct smatch_state *state);
