/*
 * sparse/smatch_extra.h
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

DECLARE_PTR_LIST(range_list, struct data_range);
DECLARE_PTR_LIST(range_list_stack, struct range_list);

struct relation {
	char *name;
	struct symbol *sym;
};

DECLARE_PTR_LIST(related_list, struct relation);

struct data_info {
	struct related_list *related;
	struct range_list *value_ranges;
	unsigned int hard_max:1;
};
DECLARE_ALLOCATOR(data_info);

extern struct string_list *__ignored_macros;

/* these are implemented in smatch_ranges.c */
char *show_rl(struct range_list *list);
int str_to_comparison_arg(const char *c, struct expression *call, int *comparison, struct expression **arg);
void str_to_rl(struct symbol *type, char *value, struct range_list **rl);
void call_results_to_rl(struct expression *call, struct symbol *type, char *value, struct range_list **rl);

struct data_range *alloc_range(sval_t min, sval_t max);
struct data_range *alloc_range_perm(sval_t min, sval_t max);

struct range_list *alloc_rl(sval_t min, sval_t max);
struct range_list *clone_rl(struct range_list *list);
struct range_list *clone_rl_permanent(struct range_list *list);
struct range_list *alloc_whole_rl(struct symbol *type);

void add_range(struct range_list **list, sval_t min, sval_t max);
struct range_list *remove_range(struct range_list *list, sval_t min, sval_t max);
void tack_on(struct range_list **list, struct data_range *drange);

int true_comparison_range(struct data_range *left, int comparison, struct data_range *right);
int true_comparison_range_LR(int comparison, struct data_range *var, struct data_range *val, int left);
int false_comparison_range_LR(int comparison, struct data_range *var, struct data_range *val, int left);

int possibly_true(struct expression *left, int comparison, struct expression *right);
int possibly_true_rl(struct range_list *left_ranges, int comparison, struct range_list *right_ranges);
int possibly_true_rl_LR(int comparison, struct range_list *a, struct range_list *b, int left);

int possibly_false(struct expression *left, int comparison, struct expression *right);
int possibly_false_rl(struct range_list *left_ranges, int comparison, struct range_list *right_ranges);
int possibly_false_rl_LR(int comparison, struct range_list *a, struct range_list *b, int left);

int rl_has_sval(struct range_list *rl, sval_t sval);
int ranges_equiv(struct data_range *one, struct data_range *two);

int rl_equiv(struct range_list *one, struct range_list *two);
int is_whole_rl(struct range_list *rl);

sval_t rl_min(struct range_list *rl);
sval_t rl_max(struct range_list *rl);
int rl_to_sval(struct range_list *rl, sval_t *sval);
struct symbol *rl_type(struct range_list *rl);

struct range_list *rl_invert(struct range_list *orig);
struct range_list *rl_filter(struct range_list *rl, struct range_list *filter);
struct range_list *rl_intersection(struct range_list *one, struct range_list *two);
struct range_list *rl_union(struct range_list *one, struct range_list *two);

void push_rl(struct range_list_stack **rl_stack, struct range_list *rl);
struct range_list *pop_rl(struct range_list_stack **rl_stack);
struct range_list *top_rl(struct range_list_stack *rl_stack);
void filter_top_rl(struct range_list_stack **rl_stack, sval_t sval);

struct range_list *rl_truncate_cast(struct symbol *type, struct range_list *rl);
struct range_list *cast_rl(struct symbol *type, struct range_list *rl);
int get_implied_rl(struct expression *expr, struct range_list **rl);
int get_absolute_rl(struct expression *expr, struct range_list **rl);
int get_implied_rl_var_sym(const char *var, struct symbol *sym, struct range_list **rl);

void free_rl(struct range_list **rlist);
void free_data_info_allocs(void);

/* smatch_estate.c */

struct smatch_state *alloc_estate_empty(void);
struct smatch_state *alloc_estate_sval(sval_t sval);
struct smatch_state *alloc_estate_range(sval_t min, sval_t max);
struct smatch_state *alloc_estate_rl(struct range_list *rl);
struct smatch_state *alloc_estate_whole(struct symbol *type);
struct smatch_state *clone_estate(struct smatch_state *state);

struct smatch_state *merge_estates(struct smatch_state *s1, struct smatch_state *s2);

int estates_equiv(struct smatch_state *one, struct smatch_state *two);
int estate_is_whole(struct smatch_state *state);

struct range_list *estate_rl(struct smatch_state *state);
struct related_list *estate_related(struct smatch_state *state);

sval_t estate_min(struct smatch_state *state);
sval_t estate_max(struct smatch_state *state);
struct symbol *estate_type(struct smatch_state *state);

int estate_has_hard_max(struct smatch_state *state);
void estate_set_hard_max(struct smatch_state *state);
void estate_clear_hard_max(struct smatch_state *state);
int estate_get_hard_max(struct smatch_state *state, sval_t *sval);

int estate_get_single_value(struct smatch_state *state, sval_t *sval);
struct smatch_state *get_implied_estate(struct expression *expr);

struct smatch_state *estate_filter_sval(struct smatch_state *orig, sval_t filter);
struct smatch_state *estate_filter_range(struct smatch_state *orig, sval_t filter_min, sval_t filter_max);

/* smatch_extra.c */
void call_extra_mod_hooks(const char *name, struct symbol *sym, struct smatch_state *state);
struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state);
struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state);
void set_extra_nomod(const char *name, struct symbol *sym, struct smatch_state *state);
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state);

struct data_info *get_dinfo(struct smatch_state *state);

void add_extra_mod_hook(void (*fn)(const char *name, struct symbol *sym, struct smatch_state *state));
int implied_not_equal(struct expression *expr, long long val);

struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct state_list **slist);
int __iterator_unchanged(struct sm_state *sm);
void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition);

/* smatch_equiv.c */
void set_equiv(struct expression *left, struct expression *right);
void set_related(struct smatch_state *estate, struct related_list *rlist);
struct related_list *get_shared_relations(struct related_list *one,
					      struct related_list *two);
struct related_list *clone_related_list(struct related_list *related);
void remove_from_equiv(const char *name, struct symbol *sym);
void remove_from_equiv_expr(struct expression *expr);
void set_equiv_state_expr(int id, struct expression *expr, struct smatch_state *state);

/* smatch_function_hooks.c */
void function_comparison(int comparison, struct expression *expr, sval_t sval, int left);

/* smatch_expressions.c */
struct expression *zero_expr();
struct expression *value_expr(long long val);
struct expression *member_expression(struct expression *deref, int op, struct ident *member);
struct expression *deref_expression(struct expression *expr);
struct expression *assign_expression(struct expression *left, struct expression *right);
struct expression *binop_expression(struct expression *left, int op, struct expression *right);
struct expression *array_element_expression(struct expression *array, struct expression *offset);
struct expression *symbol_expression(struct symbol *sym);
struct expression *unknown_value_expression(struct expression *expr);

/* smatch_param_limit.c */
struct smatch_state *get_orig_estate(const char *name, struct symbol *sym);
