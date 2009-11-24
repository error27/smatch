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
	DECLARATION_HOOK,
	ASSIGNMENT_HOOK,
	CONDITION_HOOK,
	WHOLE_CONDITION_HOOK,
	FUNCTION_CALL_HOOK,
	CALL_ASSIGNMENT_HOOK,
	OP_HOOK,
	DEREF_HOOK,
	CASE_HOOK,
	BASE_HOOK,
	FUNC_DEF_HOOK,
	END_FUNC_HOOK,
	RETURN_HOOK,
	END_FILE_HOOK,
};
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
void add_function_hook(const char *lock_for, func_hook *call_back, void *data);

void add_conditional_hook(const char *look_for, func_hook *call_back, void *data);
void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info);
void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info);
typedef void (modification_hook)(const char *name, struct symbol *sym,
				struct expression *expr, void *data);
void add_modification_hook(const char *variable, modification_hook *hook,
			void *data);
int is_member(struct expression *expr);
void reset_on_container_modified(int owner, struct expression *expr);
void set_default_state(int owner, struct smatch_state *state);

extern int final_pass;

#define sm_printf(msg...) do { if (final_pass) printf(msg); } while (0) \

#define sm_msg(msg...) \
do {                                                          \
	if (!final_pass)                                      \
		break;                                        \
	printf("%s +%d %s(%d) ", get_filename(), get_lineno(), \
	       get_function(), get_func_pos());               \
        printf(msg);                                          \
        printf("\n");                                         \
} while (0)

#define sm_debug(msg...) do { if (debug_states) printf(msg); } while (0)

#define UNDEFINED INT_MIN
#define POINTER_MAX 0xffffffff

struct smatch_state *get_state(int owner, const char *name, struct symbol *sym);
struct smatch_state *get_state_expr(int owner, struct expression *expr);
struct state_list *get_possible_states(int owner, const char *name, 
				       struct symbol *sym);
struct state_list *get_possible_states_expr(int owner, struct expression *expr);
void set_state(int owner, const char *name, struct symbol *sym, 
	       struct smatch_state *state);
void set_state_expr(int owner, struct expression *expr,
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
int is_reachable();
char *get_filename();
char *get_function();
int get_lineno();
int get_func_pos();

/* smatch_helper.c */
char *alloc_string(const char *str);
void free_string(char *str);
struct expression *get_argument_from_call_expr(struct expression_list *args,
					       int num);
char *get_variable_from_expr_complex(struct expression *expr,
				     struct symbol **sym_ptr);
char *get_variable_from_expr(struct expression *expr,
			     struct symbol **sym_ptr);
struct symbol *get_ptr_type(struct expression *expr);
int sym_name_is(const char *name, struct expression *expr);
int get_value(struct expression *expr);
int get_implied_value(struct expression *expr);
int is_zero(struct expression *expr);
int is_array(struct expression *expr);
struct expression *get_array_name(struct expression *expr);
struct expression *get_array_offset(struct expression *expr);
const char *show_state(struct smatch_state *state);
struct statement *get_block_thing(struct expression *expr);
struct expression *strip_expr(struct expression *expr);
void scoped_state(const char *name, int my_id, struct symbol *sym);
int is_error_return(struct expression *expr);

/* smatch_ignore.c */
void add_ignore(int owner, const char *name, struct symbol *sym);
int is_ignored(int owner, const char *name, struct symbol *sym);

/* smatch_tracker */
struct tracker *alloc_tracker(int owner, const char *name, struct symbol *sym);
void add_tracker(struct tracker_list **list, int owner, const char *name, 
		struct symbol *sym);
void del_tracker(struct tracker_list **list, int owner, const char *name, 
		struct symbol *sym);
int in_tracker_list(struct tracker_list *list, int owner, const char *name, 
		struct symbol *sym);
void free_tracker_list(struct tracker_list **list);
void free_trackers_and_list(struct tracker_list **list);

/* smatch_conditions */
int in_condition();

/* ----------------------------------------------------------------
   The stuff below is all used internally and shouldn't 
   be called from other programs 
 -----------------------------------------------------------------*/

/* smatch_flow.c */

void smatch (int argc, char **argv);
void __split_expr(struct expression *expr);
void __split_statements(struct statement *stmt);
extern int option_assume_loops;
extern int option_known_conditions;
extern int option_two_passes;
extern struct symbol *cur_func_sym;

/* smatch_conditions */
void __split_whole_condition(struct expression *expr);

/* smatch_implied.c */
extern int debug_implied_states;
extern int option_no_implied;
void get_implications(char *name, struct symbol *sym, int comparison, int num,
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

int get_implied_single_val(struct expression *expr);
int get_implied_max(struct expression *expr);
int get_implied_min(struct expression *expr);
int true_comparison(int left, int comparison, int right);
int known_condition_true(struct expression *expr);
int known_condition_false(struct expression *expr);
int implied_condition_true(struct expression *expr);
int implied_condition_false(struct expression *expr);

/* smatch_states.c */
extern int debug_states;

extern int __fake_cur;
extern struct state_list *__fake_cur_slist;
extern int __fake_conditions;
extern struct state_list *__fake_cond_true;
extern struct state_list *__fake_cond_false;


void __set_state(struct sm_state *sm);
struct state_list *__get_cur_slist();
void __set_true_false_sm(struct sm_state *true_state, 
			struct sm_state *false_state);
void nullify_path();	   
void __match_nullify_path_hook(const char *fn, struct expression *expr,
			       void *unused);	   
void __unnullify_path();	   
int __path_is_null();
void clear_all_states();

struct sm_state *get_sm_state(int owner, const char *name, 
				struct symbol *sym);
struct sm_state *get_sm_state_expr(int owner, struct expression *expr);
void __use_false_only_stack();
void __pop_false_only_stack();
void __push_true_states();
void __use_false_states();
void __pop_false_states();
void __merge_false_states();
void __merge_true_states();

void __negate_cond_stacks();
void __save_false_states_for_later();
void __use_previously_stored_false_states();
void __use_cond_true_states();
void __use_cond_false_states();
void __push_cond_stacks();
void __and_cond_states();
void __or_cond_states();
void __save_pre_cond_states();
void __pop_pre_cond_states();
void __use_cond_states();

void __warn_on_silly_pre_loops();

void __push_continues();
void __pop_continues();
void __process_continues();
void __merge_continues();

void __push_breaks();
void __process_breaks();
void __merge_breaks();
void __use_breaks();

void __save_switch_states(struct expression *switch_expr);
void __pop_switches();
void __merge_switches(struct expression *switch_expr, struct expression *case_expr);
void __push_default();
void __set_default();
int __pop_default();

void __push_conditions();
void __pop_conditions();

void __save_gotos(const char *name);
void __merge_gotos(const char *name);

void __print_cur_slist();

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
void create_function_hash(void);
void __match_initializer_call(struct symbol *sym);

/* smatch_files.c */
struct token *get_tokens_file(const char *filename);

/* smatch_oom.c */
extern int option_oom_kb;
int out_of_memory();

/* smatch.c */
extern char *option_project;
extern char *data_dir;
extern int option_no_data;
extern int option_spammy;
extern struct smatch_state *default_state[];

#endif 	    /* !SMATCH_H_ */
