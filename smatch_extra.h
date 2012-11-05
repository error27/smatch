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

DECLARE_PTR_LIST(range_list_sval, struct data_range_sval);
DECLARE_PTR_LIST(range_list_stack_sval, struct range_list_sval);

struct relation {
	int op;
	char *name;
	struct symbol *sym;
};

DECLARE_PTR_LIST(related_list, struct relation);

struct data_info {
	struct related_list *related;
	enum data_type type;
	struct range_list_sval *value_ranges;
};
DECLARE_ALLOCATOR(data_info);

extern struct string_list *__ignored_macros;

extern struct smatch_state estate_undefined;
void alloc_estate_undefined(void);

/* these are implemented in smatch_ranges.c */
int is_whole_range_rl_sval(struct range_list_sval *rl);
sval_t rl_min_sval(struct range_list_sval *rl);
sval_t rl_max_sval(struct range_list_sval *rl);

struct data_range *alloc_range_perm(long long min, long long max);
struct data_range_sval *alloc_range_perm_sval(sval_t min, sval_t max);
struct range_list *alloc_range_list(long long min, long long max);
struct range_list_sval *alloc_range_list_sval(sval_t min, sval_t max);
struct range_list *whole_range_list(void);
struct range_list_sval *whole_range_list_sval(void);
void add_range(struct range_list **list, long long min, long long max);
void add_range_sval(struct range_list_sval **list, sval_t min, sval_t max);
int ranges_equiv_sval(struct data_range_sval *one, struct data_range_sval *two);
int range_lists_equiv_sval(struct range_list_sval *one, struct range_list_sval *two);
int true_comparison_range(struct data_range *left, int comparison, struct data_range *right);
int true_comparison_range_sval(struct data_range_sval *left, int comparison, struct data_range_sval *right);

int possibly_true(struct expression *left, int comparison, struct expression *right);
int possibly_true_range_lists_sval(struct range_list_sval *left_ranges, int comparison, struct range_list_sval *right_ranges);
int possibly_true_range_lists_rl_sval(int comparison, struct range_list_sval *a, struct range_list_sval *b, int left);

int possibly_false(struct expression *left, int comparison, struct expression *right);
int possibly_false_range_lists_sval(struct range_list_sval *left_ranges, int comparison, struct range_list_sval *right_ranges);
int possibly_false_range_lists_rl_sval(int comparison, struct range_list_sval *a, struct range_list_sval *b, int left);

void free_range_list(struct range_list **rlist);
void free_range_list_sval(struct range_list_sval **rlist);
void free_data_info_allocs(void);
struct range_list *clone_range_list(struct range_list *list);
struct range_list_sval *clone_range_list_sval(struct range_list_sval *list);
struct range_list_sval *clone_permanent_sval(struct range_list_sval *list);
char *show_ranges(struct range_list *list);
char *show_ranges_sval(struct range_list_sval *list);
void get_value_ranges(char *value, struct range_list **rl);
void get_value_ranges_sval(char *value, struct range_list_sval **rl);

struct range_list *remove_range(struct range_list *list, long long min, long long max);
struct range_list_sval *remove_range_sval(struct range_list_sval *list, sval_t min, sval_t max);

/* used in smatch_slist.  implemented in smatch_extra.c */
int implied_not_equal(struct expression *expr, long long val);
struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct state_list **slist);
int __iterator_unchanged(struct sm_state *sm);
void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition);

/* also implemented in smatch_extra */
int estates_equiv(struct smatch_state *one, struct smatch_state *two);
struct smatch_state *clone_estate(struct smatch_state *state);
struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state);
struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state);
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state);
struct smatch_state *alloc_estate_empty(void);
struct smatch_state *alloc_estate_sval(sval_t sval);
struct smatch_state *alloc_estate_range_list_sval(struct range_list_sval *rl);
struct data_info *get_dinfo(struct smatch_state *state);
struct range_list_sval *estate_ranges_sval(struct smatch_state *state);
struct related_list *estate_related(struct smatch_state *state);
sval_t estate_min_sval(struct smatch_state *state);
sval_t estate_max_sval(struct smatch_state *state);
struct smatch_state *filter_range_list(struct smatch_state *orig,
				 struct range_list_sval *rl);
struct smatch_state *add_filter(struct smatch_state *orig, sval_t filter);
struct smatch_state *filter_range(struct smatch_state *orig, sval_t filter_min, sval_t filter_max);
struct smatch_state *extra_undefined(void);

struct range_list *range_list_union(struct range_list *one, struct range_list *two);
struct range_list_sval *range_list_union_sval(struct range_list_sval *one, struct range_list_sval *two);
int estate_get_single_value_sval(struct smatch_state *state, sval_t *sval);

void function_comparison(int comparison, struct expression *expr, sval_t sval, int left);

int true_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
int false_comparison_range_lr(int comparison, struct data_range *var, struct data_range *val, int left);
int true_comparison_range_lr_sval(int comparison, struct data_range_sval *var, struct data_range_sval *val, int left);
int false_comparison_range_lr_sval(int comparison, struct data_range_sval *var, struct data_range_sval *val, int left);
struct data_range *alloc_range(long long min, long long max);
struct data_range_sval *alloc_range_sval(sval_t min, sval_t max);
struct data_range_sval *drange_to_drange_sval(struct data_range *drange);
struct data_range *drange_sval_to_drange(struct data_range_sval *drange);
void tack_on(struct range_list **list, struct data_range *drange);
void tack_on_sval(struct range_list_sval **list, struct data_range_sval *drange);

struct smatch_state *alloc_estate_range_sval(sval_t min, sval_t max);

void push_range_list(struct range_list_stack **rl_stack, struct range_list *rl);
struct range_list *pop_range_list(struct range_list_stack **rl_stack);
struct range_list *top_range_list(struct range_list_stack *rl_stack);
void push_range_list_sval(struct range_list_stack_sval **rl_stack, struct range_list_sval *rl);
struct range_list_sval *pop_range_list_sval(struct range_list_stack_sval **rl_stack);
struct range_list_sval *top_range_list_sval(struct range_list_stack_sval *rl_stack);

void filter_top_range_list(struct range_list_stack **rl_stack, long long num);
void filter_top_range_list_sval(struct range_list_stack_sval **rl_stack, sval_t sval);
int get_implied_range_list_sval(struct expression *expr, struct range_list_sval **rl);
int is_whole_range(struct smatch_state *state);

struct range_list_sval *range_list_to_sval(struct range_list *list);
struct range_list *rl_sval_to_rl(struct range_list_sval *list);

/* smatch_expressions.c */
struct expression *zero_expr();
struct expression *value_expr(long long val);
struct expression *deref_expression(struct expression *deref, int op, struct ident *member);
struct expression *assign_expression(struct expression *left, struct expression *right);
struct expression *symbol_expression(struct symbol *sym);

/* implemented in smatch_constraints */
void set_equiv(struct expression *left, struct expression *right);
void set_related(struct smatch_state **estate, struct related_list *rlist);
struct related_list *get_shared_relations(struct related_list *one,
					      struct related_list *two);
struct related_list *clone_related_list(struct related_list *related);
void remove_from_equiv(const char *name, struct symbol *sym);
void remove_from_equiv_expr(struct expression *expr);
void set_equiv_state_expr(int id, struct expression *expr, struct smatch_state *state);

