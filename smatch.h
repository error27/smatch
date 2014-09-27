/*
 * Copyright (C) 2006 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#ifndef   	SMATCH_H_
# define   	SMATCH_H_

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include "lib.h"
#include "allocate.h"
#include "scope.h"
#include "parse.h"
#include "expression.h"
#include "avl.h"

typedef struct {
	struct symbol *type;
	union {
		long long value;
		unsigned long long uvalue;
	};
} sval_t;

struct smatch_state {
	const char *name;
	void *data;
};
#define STATE(_x) static struct smatch_state _x = { .name = #_x }
extern struct smatch_state undefined;
extern struct smatch_state merged;
extern struct smatch_state true_state;
extern struct smatch_state false_state;
DECLARE_ALLOCATOR(smatch_state);

static inline void *INT_PTR(int i)
{
	return (void *)(long)i;
}

static inline int PTR_INT(void *p)
{
	return (int)(long)p;
}

struct tracker {
	char *name;
	struct symbol *sym;
	unsigned short owner;
};
DECLARE_ALLOCATOR(tracker);
DECLARE_PTR_LIST(tracker_list, struct tracker);

/* The first 3 struct members must match struct tracker */
struct sm_state {
	const char *name;
	struct symbol *sym;
	unsigned short owner;
	unsigned short merged:1;
	unsigned short implied:1;
	unsigned int nr_children;
	unsigned int line;
  	struct smatch_state *state;
	struct stree *pool;
	struct sm_state *left;
	struct sm_state *right;
	struct state_list *possible;
};

struct var_sym {
	char *var;
	struct symbol *sym;
};
DECLARE_ALLOCATOR(var_sym);
DECLARE_PTR_LIST(var_sym_list, struct var_sym);

enum hook_type {
	EXPR_HOOK,
	STMT_HOOK,
	STMT_HOOK_AFTER,
	SYM_HOOK,
	STRING_HOOK,
	DECLARATION_HOOK,
	ASSIGNMENT_HOOK,
	RAW_ASSIGNMENT_HOOK,
	GLOBAL_ASSIGNMENT_HOOK,
	LOGIC_HOOK,
	CONDITION_HOOK,
	PRELOOP_HOOK,
	SELECT_HOOK,
	WHOLE_CONDITION_HOOK,
	FUNCTION_CALL_HOOK,
	CALL_HOOK_AFTER_INLINE,
	CALL_ASSIGNMENT_HOOK,
	MACRO_ASSIGNMENT_HOOK,
	BINOP_HOOK,
	OP_HOOK,
	DEREF_HOOK,
	CASE_HOOK,
	ASM_HOOK,
	CAST_HOOK,
	SIZEOF_HOOK,
	BASE_HOOK,
	FUNC_DEF_HOOK,
	AFTER_DEF_HOOK,
	END_FUNC_HOOK,
	AFTER_FUNC_HOOK,
	RETURN_HOOK,
	INLINE_FN_START,
	INLINE_FN_END,
	END_FILE_HOOK,
};

#define TRUE 1
#define FALSE 0

struct range_list;

void add_hook(void *func, enum hook_type type);
typedef struct smatch_state *(merge_func_t)(struct smatch_state *s1, struct smatch_state *s2);
typedef struct smatch_state *(unmatched_func_t)(struct sm_state *state);
void add_merge_hook(int client_id, merge_func_t *func);
void add_unmatched_state_hook(int client_id, unmatched_func_t *func);
typedef void (scope_hook)(void *data);
void add_scope_hook(scope_hook *hook, void *data);
typedef void (func_hook)(const char *fn, struct expression *expr, void *data);
typedef void (implication_hook)(const char *fn, struct expression *call_expr,
				struct expression *assign_expr, void *data);
typedef void (return_implies_hook)(struct expression *call_expr,
				   int param, char *key, char *value);
typedef int (implied_return_hook)(struct expression *call_expr, void *info, struct range_list **rl);
void add_function_hook(const char *look_for, func_hook *call_back, void *data);

void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info);
void add_implied_return_hook(const char *look_for,
			     implied_return_hook *call_back,
			     void *info);
void add_macro_assign_hook(const char *look_for, func_hook *call_back,
			      void *info);
void add_macro_assign_hook_extra(const char *look_for, func_hook *call_back,
			      void *info);
void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info);
void select_return_states_hook(int type, return_implies_hook *callback);
void select_return_states_before(void (*fn)(void));
void select_return_states_after(void (*fn)(void));
int get_implied_return(struct expression *expr, struct range_list **rl);

typedef void (modification_hook)(struct sm_state *sm, struct expression *mod_expr);
void add_modification_hook(int owner, modification_hook *call_back);
void add_indirect_modification_hook(int owner, modification_hook *call_back);

int outside_of_function(void);
const char *get_filename(void);
const char *get_base_file(void);
char *get_function(void);
int get_lineno(void);
extern int final_pass;
extern struct symbol *cur_func_sym;
extern int option_debug;
extern int local_debug;
extern int option_info;
extern struct stree *global_states;
int is_silenced_function(void);

extern FILE *sm_outfd;
#define sm_printf(msg...) do { if (final_pass || option_debug) fprintf(sm_outfd, msg); } while (0)

static inline void sm_prefix(void)
{
	sm_printf("%s:%d %s() ", get_filename(), get_lineno(), get_function());
}

static inline void print_implied_debug_msg();

#define sm_msg(msg...) \
do {                                                           \
	print_implied_debug_msg();                             \
	if (!option_debug && !final_pass)                      \
		break;                                         \
	if (!option_info && is_silenced_function())	       \
		break;					       \
	sm_prefix();					       \
        sm_printf(msg);                                        \
        sm_printf("\n");                                       \
} while (0)

#define local_debug(msg...)					\
do {								\
	if (local_debug)					\
		sm_msg(msg);					\
} while (0)

extern char *implied_debug_msg;
static inline void print_implied_debug_msg(void)
{
	static struct symbol *last_printed = NULL;

	if (!implied_debug_msg)
		return;
	if (last_printed == cur_func_sym)
		return;
	last_printed = cur_func_sym;
	sm_msg("%s", implied_debug_msg);
}

#define sm_debug(msg...) do { if (option_debug) sm_printf(msg); } while (0)

#define sm_info(msg...) do {					\
	if (option_debug || (option_info && final_pass)) {	\
		sm_prefix();					\
		sm_printf("info: ");				\
		sm_printf(msg);					\
		sm_printf("\n");				\
	}							\
} while(0)

struct smatch_state *get_state(int owner, const char *name, struct symbol *sym);
struct smatch_state *get_state_expr(int owner, struct expression *expr);
struct state_list *get_possible_states(int owner, const char *name,
				       struct symbol *sym);
struct state_list *get_possible_states_expr(int owner, struct expression *expr);
struct sm_state *set_state(int owner, const char *name, struct symbol *sym,
	       struct smatch_state *state);
struct sm_state *set_state_expr(int owner, struct expression *expr,
		struct smatch_state *state);
void delete_state(int owner, const char *name, struct symbol *sym);
void delete_state_expr(int owner, struct expression *expr);
void set_true_false_states(int owner, const char *name, struct symbol *sym,
			   struct smatch_state *true_state,
			   struct smatch_state *false_state);
void set_true_false_states_expr(int owner, struct expression *expr,
			   struct smatch_state *true_state,
			   struct smatch_state *false_state);

struct stree *get_all_states_from_stree(int owner, struct stree *source);
struct stree *get_all_states_stree(int id);
struct stree *__get_cur_stree(void);
int is_reachable(void);

/* smatch_helper.c */
char *alloc_string(const char *str);
void free_string(char *str);
void append(char *dest, const char *data, int buff_len);
void remove_parens(char *str);
struct smatch_state *alloc_state_num(int num);
struct expression *get_argument_from_call_expr(struct expression_list *args,
					       int num);

char *expr_to_var(struct expression *expr);
char *expr_to_str(struct expression *expr);
char *expr_to_str_sym(struct expression *expr,
				     struct symbol **sym_ptr);
char *expr_to_var_sym(struct expression *expr,
			     struct symbol **sym_ptr);

int sym_name_is(const char *name, struct expression *expr);
int get_const_value(struct expression *expr, sval_t *sval);
int get_value(struct expression *expr, sval_t *val);
int get_implied_value(struct expression *expr, sval_t *val);
int get_implied_min(struct expression *expr, sval_t *sval);
int get_implied_max(struct expression *expr, sval_t *val);
int get_hard_max(struct expression *expr, sval_t *sval);
int get_fuzzy_min(struct expression *expr, sval_t *min);
int get_fuzzy_max(struct expression *expr, sval_t *max);
int get_absolute_min(struct expression *expr, sval_t *sval);
int get_absolute_max(struct expression *expr, sval_t *sval);
int parse_call_math(struct expression *expr, char *math, sval_t *val);
int parse_call_math_rl(struct expression *call, char *math, struct range_list **rl);
char *get_value_in_terms_of_parameter_math(struct expression *expr);
char *get_value_in_terms_of_parameter_math_var_sym(const char *var, struct symbol *sym);
int is_zero(struct expression *expr);
int known_condition_true(struct expression *expr);
int known_condition_false(struct expression *expr);
int implied_condition_true(struct expression *expr);
int implied_condition_false(struct expression *expr);

int is_array(struct expression *expr);
struct expression *get_array_name(struct expression *expr);
struct expression *get_array_offset(struct expression *expr);
const char *show_state(struct smatch_state *state);
struct statement *get_expression_statement(struct expression *expr);
struct expression *strip_parens(struct expression *expr);
struct expression *strip_expr(struct expression *expr);
void scoped_state(int my_id, const char *name, struct symbol *sym);
int is_error_return(struct expression *expr);
int getting_address(void);
char *get_member_name(struct expression *expr);
char *get_fnptr_name(struct expression *expr);
int cmp_pos(struct position pos1, struct position pos2);
int positions_eq(struct position pos1, struct position pos2);
struct statement *get_current_statement(void);
struct statement *get_prev_statement(void);
int get_param_num_from_sym(struct symbol *sym);
int get_param_num(struct expression *expr);
int ms_since(struct timeval *start);

/* smatch_type.c */
struct symbol *get_real_base_type(struct symbol *sym);
int type_bits(struct symbol *type);
int type_bytes(struct symbol *type);
int type_positive_bits(struct symbol *type);
struct symbol *get_pointer_type(struct expression *expr);
struct symbol *get_type(struct expression *expr);
int type_unsigned(struct symbol *base_type);
int type_signed(struct symbol *base_type);
int expr_unsigned(struct expression *expr);
int returns_unsigned(struct symbol *base_type);
int is_pointer(struct expression *expr);
int returns_pointer(struct symbol *base_type);
sval_t sval_type_max(struct symbol *base_type);
sval_t sval_type_min(struct symbol *base_type);
int nr_bits(struct expression *expr);
int is_void_pointer(struct expression *expr);
int is_char_pointer(struct expression *expr);
int is_string(struct expression *expr);
int is_static(struct expression *expr);
int types_equiv(struct symbol *one, struct symbol *two);
int fn_static(void);
const char *global_static();
struct symbol *cur_func_return_type(void);
struct symbol *get_arg_type(struct expression *fn, int arg);
struct symbol *get_member_type_from_key(struct expression *expr, char *key);
int is_struct(struct expression *expr);
char *type_to_str(struct symbol *sym);

/* smatch_ignore.c */
void add_ignore(int owner, const char *name, struct symbol *sym);
int is_ignored(int owner, const char *name, struct symbol *sym);

/* smatch_var_sym */
struct var_sym *alloc_var_sym(const char *var, struct symbol *sym);
struct var_sym_list *expr_to_vsl(struct expression *expr);
void add_var_sym(struct var_sym_list **list, const char *var, struct symbol *sym);
void add_var_sym_expr(struct var_sym_list **list, struct expression *expr);
void del_var_sym(struct var_sym_list **list, const char *var, struct symbol *sym);
int in_var_sym_list(struct var_sym_list *list, const char *var, struct symbol *sym);
struct var_sym_list *clone_var_sym_list(struct var_sym_list *from_vsl);
void merge_var_sym_list(struct var_sym_list **dest, struct var_sym_list *src);
struct var_sym_list *combine_var_sym_lists(struct var_sym_list *one, struct var_sym_list *two);
void free_var_sym_list(struct var_sym_list **list);
void free_var_syms_and_list(struct var_sym_list **list);

/* smatch_tracker */
struct tracker *alloc_tracker(int owner, const char *name, struct symbol *sym);
void add_tracker(struct tracker_list **list, int owner, const char *name,
		struct symbol *sym);
void add_tracker_expr(struct tracker_list **list, int owner, struct expression *expr);
void del_tracker(struct tracker_list **list, int owner, const char *name,
		struct symbol *sym);
int in_tracker_list(struct tracker_list *list, int owner, const char *name,
		struct symbol *sym);
void free_tracker_list(struct tracker_list **list);
void free_trackers_and_list(struct tracker_list **list);

/* smatch_conditions */
int in_condition(void);

/* smatch_flow.c */

extern int __in_fake_assign;
void smatch (int argc, char **argv);
int inside_loop(void);
int definitely_inside_loop(void);
struct expression *get_switch_expr(void);
int in_expression_statement(void);
void __process_post_op_stack(void);
void __split_expr(struct expression *expr);
void __split_stmt(struct statement *stmt);
extern int __in_function_def;
extern int option_assume_loops;
extern int option_known_conditions;
extern int option_two_passes;
extern int option_no_db;
extern int option_file_output;
extern int option_time;
extern struct expression_list *big_expression_stack;
extern struct statement_list *big_statement_stack;
int inlinable(struct expression *expr);
extern int __inline_call;
extern struct expression *__inline_fn;
extern int __in_pre_condition;
extern int __bail_on_rest_of_function;

/* smatch_struct_assignment.c */
struct expression *get_faked_expression(void);
void __fake_struct_member_assignments(struct expression *expr);

/* smatch_project.c */
int is_no_inline_function(const char *function);

/* smatch_conditions */
void __split_whole_condition(struct expression *expr);
void __handle_logic(struct expression *expr);
int is_condition(struct expression *expr);
int __handle_condition_assigns(struct expression *expr);
int __handle_select_assigns(struct expression *expr);
int __handle_expr_statement_assigns(struct expression *expr);

/* smatch_implied.c */
extern int option_debug_implied;
extern int option_debug_related;
extern int option_no_implied;
struct range_list_stack;
struct stree *__implied_case_stree(struct expression *switch_expr,
				 struct expression *case_expr,
				 struct range_list_stack **remaining_cases,
				 struct stree **raw_stree);
struct range_list *__get_implied_values(struct expression *switch_expr);
void overwrite_states_using_pool(struct sm_state *sm);

/* smatch_extras.c */
#define SMATCH_EXTRA 1 /* this is my_id from smatch extra set in smatch.c */

struct data_range {
	sval_t min;
	sval_t max;
};

static const long long valid_ptr_min = 4096;
static const long long valid_ptr_max = LONG_MAX;
static const sval_t valid_ptr_min_sval = {
	.type = &ptr_ctype,
	{.value = 4096},
};
static const sval_t valid_ptr_max_sval = {
	.type = &ptr_ctype,
	{.value = LONG_MAX},
};
static const sval_t array_min_sval = {
	.type = &ptr_ctype,
	{.value = 100000},
};
static const sval_t array_max_sval = {
	.type = &ptr_ctype,
	{.value = 200000},
};

/* smatch_absolute.c */
int get_absolute_min_helper(struct expression *expr, sval_t *sval);
int get_absolute_max_helper(struct expression *expr, sval_t *sval);

/* smatch_local_values.c */
int get_local_rl(struct expression *expr, struct range_list **rl);
int get_local_max_helper(struct expression *expr, sval_t *sval);
int get_local_min_helper(struct expression *expr, sval_t *sval);

/* smatch_type_value.c */
int get_db_type_rl(struct expression *expr, struct range_list **rl);

/* smatch_states.c */
void __push_fake_cur_stree();
struct stree *__pop_fake_cur_stree();
void __free_fake_cur_stree();
void __set_fake_cur_stree_fast(struct stree *stree);
void __pop_fake_cur_stree_fast(void);
void __merge_stree_into_cur(struct stree *stree);

int unreachable(void);
void __set_sm(struct sm_state *sm);
void __set_true_false_sm(struct sm_state *true_state,
			struct sm_state *false_state);
void nullify_path(void);
void __match_nullify_path_hook(const char *fn, struct expression *expr,
			       void *unused);
void __unnullify_path(void);
int __path_is_null(void);
void save_all_states(void);
void restore_all_states(void);
void free_goto_stack(void);
void clear_all_states(void);

struct sm_state *get_sm_state(int owner, const char *name,
				struct symbol *sym);
struct sm_state *get_sm_state_expr(int owner, struct expression *expr);
void __push_true_states(void);
void __use_false_states(void);
void __discard_false_states(void);
void __merge_false_states(void);
void __merge_true_states(void);

void __negate_cond_stacks(void);
void __use_pre_cond_states(void);
void __use_cond_true_states(void);
void __use_cond_false_states(void);
void __push_cond_stacks(void);
struct stree *__copy_cond_true_states(void);
struct stree *__copy_cond_false_states(void);
struct stree *__pop_cond_true_stack(void);
struct stree *__pop_cond_false_stack(void);
void __and_cond_states(void);
void __or_cond_states(void);
void __save_pre_cond_states(void);
void __discard_pre_cond_states(void);
void __use_cond_states(void);
extern struct state_list *__last_base_slist;

void __warn_on_silly_pre_loops(void);

void __push_continues(void);
void __discard_continues(void);
void __process_continues(void);
void __merge_continues(void);

void __push_breaks(void);
void __process_breaks(void);
int __has_breaks(void);
void __merge_breaks(void);
void __use_breaks(void);

void __save_switch_states(struct expression *switch_expr);
void __discard_switches(void);
void __merge_switches(struct expression *switch_expr, struct expression *case_expr);
void __push_default(void);
void __set_default(void);
int __pop_default(void);

void __push_conditions(void);
void __discard_conditions(void);

void __save_gotos(const char *name);
void __merge_gotos(const char *name);

void __print_cur_stree(void);

/* smatch_hooks.c */
void __pass_to_client(void *data, enum hook_type type);
void __pass_to_client_no_data(enum hook_type type);
void __pass_case_to_client(struct expression *switch_expr,
			   struct expression *case_expr);
int __has_merge_function(int client_id);
struct smatch_state *__client_merge_function(int owner,
					     struct smatch_state *s1,
					     struct smatch_state *s2);
struct smatch_state *__client_unmatched_state_function(struct sm_state *sm);
void __push_scope_hooks(void);
void __call_scope_hooks(void);

/* smatch_function_hooks.c */
void create_function_hook_hash(void);
void __match_initializer_call(struct symbol *sym);

/* smatch_db.c */
enum info_type {
	INTERNAL	= 0,
	/*
	 * Changing these numbers is a pain.  Don't do it.  If you ever use a
	 * number it can't be re-used right away so there may be gaps.
	 * We select these in order by type so if the order matters, then give
	 * it a number below 100-999,9000-9999 ranges. */

	PARAM_CLEARED	= 101,
	FILTER_VALUE	= 102,

	PARAM_VALUE	= 1001,
	BUF_SIZE	= 1002,
	USER_DATA	= 1003,
	CAPPED_DATA	= 1004,
	RETURN_VALUE	= 1005,
	DEREFERENCE	= 1006,
	RANGE_CAP	= 1007,
	LOCK_HELD	= 1008,
	LOCK_RELEASED	= 1009,
	ABSOLUTE_LIMITS	= 1010,
	LIMITED_VALUE	= 1011,
	ADDED_VALUE	= 1012,
	PARAM_FREED	= 1013,
	DATA_SOURCE	= 1014,
	FUZZY_MAX	= 1015,
	STR_LEN		= 1016,

	/* put random temporary stuff in the 7000-7999 range for testing */
	USER_DATA2	= 9017,
	USER_DATA_SET	= 9018,
	USER_DATA_LIMITED = 9019,
};

void debug_sql(const char *sql);
void select_caller_info_hook(void (*callback)(const char *name, struct symbol *sym, char *key, char *value), int type);
void add_member_info_callback(int owner, void (*callback)(struct expression *call, int param, char *printed_name, struct smatch_state *state));
void add_split_return_callback(void (*fn)(int return_id, char *return_ranges, struct expression *returned_expr));
void add_returned_member_callback(int owner, void (*callback)(int return_id, char *return_ranges, struct expression *expr, char *printed_name, struct smatch_state *state));
void select_call_implies_hook(int type, void (*callback)(struct expression *arg, char *key, char *value));
struct range_list *db_return_vals(struct expression *expr);
char *return_state_to_var_sym(struct expression *expr, int param, char *key, struct symbol **sym);
char *get_variable_from_key(struct expression *arg, char *key, struct symbol **sym);
const char *get_param_name(struct sm_state *sm);

#define run_sql(call_back, data, sql...)    \
do {                                  \
	char sql_txt[1024];           \
	if (option_no_db)             \
		break;                \
	snprintf(sql_txt, 1024, sql); \
	debug_sql(sql_txt);  	      \
	sql_exec(call_back, data, sql_txt); \
} while (0)

/* like run_sql() but for the in-memory database */
#define mem_sql(call_back, data, sql...)						\
do {										\
	char sql_txt[1024];							\
										\
	snprintf(sql_txt, sizeof(sql_txt), sql);				\
	sm_debug("in-mem: %s\n", sql_txt);					\
	sql_mem_exec(call_back, data, sql_txt);					\
} while (0)

void sql_insert_return_states(int return_id, const char *return_ranges,
		int type, int param, const char *key, const char *value);
void sql_insert_caller_info(struct expression *call, int type, int param,
		const char *key, const char *value);
void sql_insert_function_ptr(const char *fn, const char *struct_name);
void sql_insert_return_values(const char *return_values);
void sql_insert_call_implies(int type, int param, const char *key, const char *value);
void sql_insert_function_type_size(const char *member, const char *ranges);
void sql_insert_local_values(const char *name, const char *value);
void sql_insert_function_type_value(const char *type, const char *value);
void sql_insert_function_type_info(int param, const char *value);

void sql_select_return_states(const char *cols, struct expression *call,
	int (*callback)(void*, int, char**, char**), void *info);
void sql_select_caller_info(const char *cols, struct symbol *sym,
	int (*callback)(void*, int, char**, char**));
void sql_select_call_implies(const char *cols, struct expression *call,
	int (*callback)(void*, int, char**, char**));

void sql_exec(int (*callback)(void*, int, char**, char**), void *data, const char *sql);
void sql_mem_exec(int (*callback)(void*, int, char**, char**), void *data, const char *sql);

void open_smatch_db(void);

/* smatch_files.c */
int open_data_file(const char *filename);
struct token *get_tokens_file(const char *filename);

/* smatch.c */
extern char *option_debug_check;
extern char *option_project_str;
extern char *data_dir;
extern int option_no_data;
extern int option_spammy;
extern int option_full_path;
extern int option_param_mapper;
extern int option_call_tree;
extern int num_checks;

enum project_type {
	PROJ_NONE,
	PROJ_KERNEL,
	PROJ_WINE,
};
extern enum project_type option_project;
const char *check_name(unsigned short id);


/* smatch_buf_size.c */
int get_array_size(struct expression *expr);
int get_array_size_bytes(struct expression *expr);
int get_array_size_bytes_min(struct expression *expr);
int get_array_size_bytes_max(struct expression *expr);
struct range_list *get_array_size_bytes_rl(struct expression *expr);
int get_real_array_size(struct expression *expr);
/* smatch_strlen.c */
int get_implied_strlen(struct expression *expr, struct range_list **rl);
int get_size_from_strlen(struct expression *expr);

/* smatch_capped.c */
int is_capped(struct expression *expr);
int is_capped_var_sym(const char *name, struct symbol *sym);

/* check_user_data.c */
int is_user_macro(struct expression *expr);
int is_user_data(struct expression *expr);
int is_capped_user_data(struct expression *expr);
int implied_user_data(struct expression *expr, struct range_list **rl);

/* check_locking.c */
void print_held_locks();

/* check_assigned_expr.c */
struct expression *get_assigned_expr(struct expression *expr);
struct expression *get_assigned_expr_name_sym(const char *name, struct symbol *sym);

/* smatch_comparison.c */
int get_comparison(struct expression *left, struct expression *right);
int get_comparison_strings(const char *one, const char *two);
int possible_comparison(struct expression *a, int comparison, struct expression *b);
void __add_comparison_info(struct expression *expr, struct expression *call, const char *range);
char *expr_equal_to_param(struct expression *expr, int ignore);
char *expr_lte_to_param(struct expression *expr, int ignore);

/* smatch_sval.c */
sval_t *sval_alloc(sval_t sval);
sval_t *sval_alloc_permanent(sval_t sval);
sval_t sval_blank(struct expression *expr);
sval_t sval_type_val(struct symbol *type, long long val);
sval_t sval_from_val(struct expression *expr, long long val);
int sval_unsigned(sval_t sval);
int sval_signed(sval_t sval);
int sval_bits(sval_t sval);
int sval_positive_bits(sval_t sval);
int sval_bits_used(sval_t sval);
int sval_is_negative(sval_t sval);
int sval_is_positive(sval_t sval);
int sval_is_min(sval_t sval);
int sval_is_max(sval_t sval);
int sval_is_a_min(sval_t sval);
int sval_is_a_max(sval_t sval);
int sval_is_negative_min(sval_t sval);
int sval_cmp(sval_t one, sval_t two);
int sval_cmp_t(struct symbol *type, sval_t one, sval_t two);
int sval_cmp_val(sval_t one, long long val);
sval_t sval_min(sval_t one, sval_t two);
sval_t sval_max(sval_t one, sval_t two);
int sval_too_low(struct symbol *type, sval_t sval);
int sval_too_high(struct symbol *type, sval_t sval);
int sval_fits(struct symbol *type, sval_t sval);
sval_t sval_cast(struct symbol *type, sval_t sval);
sval_t sval_preop(sval_t sval, int op);
sval_t sval_binop(sval_t left, int op, sval_t right);
int sval_binop_overflows(sval_t left, int op, sval_t right);
const char *sval_to_str(sval_t sval);
const char *sval_to_numstr(sval_t sval);
sval_t ll_to_sval(long long val);

/* smatch_string_list.c */
int list_has_string(struct string_list *str_list, char *str);
void insert_string(struct string_list **str_list, char *str);
struct string_list *clone_str_list(struct string_list *orig);
struct string_list *combine_string_lists(struct string_list *one, struct string_list *two);

/* smatch_start_states.c */
struct stree *get_start_states(void);

/* smatch_recurse.c */
int has_symbol(struct expression *expr, struct symbol *sym);
int has_variable(struct expression *expr, struct expression *var);
int has_inc_dec(struct expression *expr);

/* smatch_stored_conditions.c */
struct smatch_state *get_stored_condition(struct expression *expr);

/* check_string_len.c */
int get_formatted_string_size(struct expression *call, int arg);

/* smatch_param_set.c */
int param_was_set(struct expression *expr);

/* smatch_links.c */
void set_up_link_functions(int id, int linkid);
void store_link(int link_id, const char *name, struct symbol *sym, const char *link_name, struct symbol *link_sym);

#endif 	    /* !SMATCH_H_ */
