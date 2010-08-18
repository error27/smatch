/*
 * sparse/smatch.h
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#ifndef   	SMATCH_H_
# define   	SMATCH_H_

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "lib.h"
#include "allocate.h"
#include "parse.h"
#include "expression.h"

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

struct sm_state {
	unsigned short owner;
	unsigned int merged:1;
	unsigned int implied:1;
        const char *name;
	struct symbol *sym;
  	struct smatch_state *state;
	unsigned int line;
	struct state_list *my_pool;
	struct sm_state *left;
	struct sm_state *right;
	unsigned int nr_children;
	struct state_list *possible;
};

struct tracker {
	int owner;
	char *name;
	struct symbol *sym;
};
DECLARE_ALLOCATOR(tracker);
DECLARE_PTR_LIST(tracker_list, struct tracker);

enum hook_type {
	EXPR_HOOK,
	STMT_HOOK,
	SYM_HOOK,
	STRING_HOOK,
	DECLARATION_HOOK,
	ASSIGNMENT_HOOK,
	LOGIC_HOOK,
	CONDITION_HOOK,
	WHOLE_CONDITION_HOOK,
	FUNCTION_CALL_HOOK,
	CALL_ASSIGNMENT_HOOK,
	BINOP_HOOK,
	OP_HOOK,
	DEREF_HOOK,
	CASE_HOOK,
	BASE_HOOK,
	FUNC_DEF_HOOK,
	END_FUNC_HOOK,
	RETURN_HOOK,
	END_FILE_HOOK,
};

#define TRUE 1
#define FALSE 0

void add_hook(void *func, enum hook_type type);
typedef struct smatch_state *(merge_func_t)(const char *name, 
					    struct symbol *sym,
					    struct smatch_state *s1, 
					    struct smatch_state *s2);
typedef struct smatch_state *(unmatched_func_t)(struct sm_state *state);
void add_merge_hook(int client_id, merge_func_t *func);
void add_unmatched_state_hook(int client_id, unmatched_func_t *func);
typedef void (scope_hook)(void *data);
void add_scope_hook(scope_hook *hook, void *data);
typedef void (func_hook)(const char *fn, struct expression *expr, void *data);
typedef void (implication_hook)(const char *fn, struct expression *call_expr,
				struct expression *assign_expr, void *data);
void add_function_hook(const char *look_for, func_hook *call_back, void *data);

void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info);
void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info);
typedef void (modification_hook)(const char *name, struct symbol *sym,
				struct expression *expr, void *data);
void add_modification_hook(int owner, const char *variable, modification_hook *hook,
			void *data);
void add_modification_hook_expr(int owner, struct expression *expr, modification_hook *call_back, void *info);
void set_default_modification_hook(int owner, modification_hook *call_back);
void __use_default_modification_hook(int owner, const char *variable);

const char *get_filename(void);
char *get_function(void);
int get_lineno(void);
int get_func_pos(void);
extern int final_pass;
extern struct symbol *cur_func_sym;
extern int option_debug;

#define sm_printf(msg...) do { if (final_pass) printf(msg); } while (0) \

static inline void sm_prefix(void)
{
	printf("%s +%d %s(%d) ", get_filename(), get_lineno(), get_function(), get_func_pos());
}


static inline void print_implied_debug_msg();

#define sm_msg(msg...) \
do {                                                           \
	print_implied_debug_msg();                             \
	if (!option_debug && !final_pass)                      \
		break;                                         \
	sm_prefix();					       \
        printf(msg);                                           \
        printf("\n");                                          \
} while (0)

extern char *implied_debug_msg;
static inline void print_implied_debug_msg()
{
	static struct symbol *last_printed = NULL;

	if (!implied_debug_msg)
		return;
	if (last_printed == cur_func_sym)
		return;
	last_printed = cur_func_sym;
	sm_msg("%s", implied_debug_msg);
}

#define sm_debug(msg...) do { if (option_debug) printf(msg); } while (0)

#define sm_info(msg...) do {					\
	if (option_debug || (option_info && final_pass)) {	\
		sm_prefix();					\
		printf("info: ");				\
		printf(msg);					\
		printf("\n");					\
	}							\
} while(0)

#define POINTER_MAX 0xffffffff

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

struct state_list *get_all_states(int id);
int is_reachable(void);

/* smatch_helper.c */
char *alloc_string(const char *str);
void free_string(char *str);
struct smatch_state *alloc_state_num(int num);
struct expression *get_argument_from_call_expr(struct expression_list *args,
					       int num);
char *get_variable_from_expr_complex(struct expression *expr,
				     struct symbol **sym_ptr);
char *get_variable_from_expr(struct expression *expr,
			     struct symbol **sym_ptr);
int sym_name_is(const char *name, struct expression *expr);
int get_value(struct expression *expr, long long *val);
int get_implied_value(struct expression *expr, long long *val);
int get_implied_max(struct expression *expr, long long *val);
int get_implied_min(struct expression *expr, long long *val);
int get_fuzzy_min(struct expression *expr, long long *min);
int get_fuzzy_max(struct expression *expr, long long *max);
int get_absolute_min(struct expression *expr, long long *val);
int get_absolute_max(struct expression *expr, long long *val);
int is_zero(struct expression *expr);
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

/* smatch_type.c */
struct symbol *get_type(struct expression *expr);
int type_unsigned(struct symbol *base_type);
int returns_unsigned(struct symbol *base_type);
int returns_pointer(struct symbol *base_type);
long long type_max(struct symbol *base_type);
long long type_min(struct symbol *base_type);

/* smatch_ignore.c */
void add_ignore(int owner, const char *name, struct symbol *sym);
int is_ignored(int owner, const char *name, struct symbol *sym);

/* smatch_tracker */
struct tracker *alloc_tracker(int owner, const char *name, struct symbol *sym);
void add_tracker(struct tracker_list **list, int owner, const char *name, 
		struct symbol *sym);
void add_tracker_expr(struct tracker_list **list, int owner, struct expression *expr);
void del_tracker(struct tracker_list **list, int owner, const char *name, 
		struct symbol *sym);
int in_tracker_list(struct tracker_list *list, int owner, const char *name, 
		struct symbol *sym);
struct tracker_list *clone_tracker_list(struct tracker_list *orig_list);
void free_tracker_list(struct tracker_list **list);
void free_trackers_and_list(struct tracker_list **list);

/* smatch_conditions */
int in_condition(void);

/* smatch_flow.c */

void smatch (int argc, char **argv);
int inside_loop(void);
int in_expression_statement(void);
void __split_expr(struct expression *expr);
void __split_stmt(struct statement *stmt);
extern int option_assume_loops;
extern int option_known_conditions;
extern int option_two_passes;
extern struct expression_list *big_expression_stack;
extern struct statement_list *big_statement_stack;
extern int __in_pre_condition;
extern int __bail_on_rest_of_function;

/* smatch_conditions */
void __split_whole_condition(struct expression *expr);
void __handle_logic(struct expression *expr);
int __handle_condition_assigns(struct expression *expr);
int __handle_select_assigns(struct expression *expr);
int __handle_expr_statement_assigns(struct expression *expr);

/* smatch_implied.c */
extern int option_debug_implied;
extern int option_no_implied;
void get_implications(char *name, struct symbol *sym, int comparison, long long num,
		      struct state_list **true_states,
		      struct state_list **false_states);
struct range_list_stack;
struct state_list *__implied_case_slist(struct expression *switch_expr,
					struct expression *case_expr,
					struct range_list_stack **remaining_cases,
					struct state_list **raw_slist);
struct range_list *__get_implied_values(struct expression *switch_expr);

/* smatch_extras.c */
#define SMATCH_EXTRA 1 /* this is my_id from smatch extra set in smatch.c */

struct data_range {
	long long min;
	long long max;
};
extern struct data_range whole_range;

int known_condition_true(struct expression *expr);
int known_condition_false(struct expression *expr);
int implied_condition_true(struct expression *expr);
int implied_condition_false(struct expression *expr);

/* smatch_states.c */
void __push_fake_cur_slist();
struct state_list *__pop_fake_cur_slist();

int unreachable(void);
void __set_sm(struct sm_state *sm);
struct state_list *__get_cur_slist(void);
void __set_true_false_sm(struct sm_state *true_state, 
			struct sm_state *false_state);
void nullify_path(void);	   
void __match_nullify_path_hook(const char *fn, struct expression *expr,
			       void *unused);	   
void __unnullify_path(void);	   
int __path_is_null(void);
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
struct state_list *__copy_cond_true_states(void);
struct state_list *__copy_cond_false_states(void);
struct state_list *__pop_cond_true_stack(void);
struct state_list *__pop_cond_false_stack(void);
void __and_cond_states(void);
void __or_cond_states(void);
void __save_pre_cond_states(void);
void __discard_pre_cond_states(void);
void __use_cond_states(void);

void __warn_on_silly_pre_loops(void);

void __push_continues(void);
void __discard_continues(void);
void __process_continues(void);
void __merge_continues(void);

void __push_breaks(void);
void __process_breaks(void);
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

void __print_cur_slist(void);

/* smatch_hooks.c */
void __pass_to_client(void *data, enum hook_type type);
void __pass_to_client_no_data(enum hook_type type);
void __pass_case_to_client(struct expression *switch_expr,
			   struct expression *case_expr);
int __has_merge_function(int client_id);
struct smatch_state *__client_merge_function(int owner, const char *name,
					     struct symbol *sym,
					     struct smatch_state *s1,
					     struct smatch_state *s2);
struct smatch_state *__client_unmatched_state_function(struct sm_state *sm);
void __push_scope_hooks(void);
void __call_scope_hooks(void);

/* smatch_function_hooks.c */
void create_function_hook_hash(void);
void __match_initializer_call(struct symbol *sym);

/* smatch_files.c */
struct token *get_tokens_file(const char *filename);

/* smatch_oom.c */
extern int option_oom_kb;
int out_of_memory(void);

/* smatch.c */
extern char *option_project_str;
extern char *data_dir;
extern int option_no_data;
extern int option_spammy;
extern int option_full_path;
extern int option_param_mapper;
extern int option_print_returns;
extern int option_info;
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

/* check_locking.c */
void print_held_locks();

#endif 	    /* !SMATCH_H_ */
