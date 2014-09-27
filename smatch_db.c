/*
 * Copyright (C) 2010 Dan Carpenter.
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

#include <string.h>
#include <errno.h>
#include <sqlite3.h>
#include <unistd.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static sqlite3 *db;
static sqlite3 *mem_db;

static int return_id;

#define sql_insert(table, values...)						\
do {										\
	if (!mem_db)								\
		break;								\
	if (__inline_fn) {							\
		char buf[1024];							\
		char *err, *p = buf;						\
		int rc;								\
										\
		p += snprintf(p, buf + sizeof(buf) - p,				\
			      "insert into %s values (", #table);		\
		p += snprintf(p, buf + sizeof(buf) - p, values);		\
		p += snprintf(p, buf + sizeof(buf) - p, ");");			\
		sm_debug("in-mem: %s\n", buf);					\
		rc = sqlite3_exec(mem_db, buf, NULL, 0, &err);			\
		if (rc != SQLITE_OK) {						\
			fprintf(stderr, "SQL error #2: %s\n", err);		\
			fprintf(stderr, "SQL: '%s'\n", buf);			\
		}								\
		break;								\
	}									\
	if (option_info) {							\
		sm_prefix();							\
	        sm_printf("SQL: insert into " #table " values (" values);	\
	        sm_printf(");\n");						\
	}									\
} while (0)

struct def_callback {
	int hook_type;
	void (*callback)(const char *name, struct symbol *sym, char *key, char *value);
};
ALLOCATOR(def_callback, "definition db hook callbacks");
DECLARE_PTR_LIST(callback_list, struct def_callback);
static struct callback_list *callbacks;

struct member_info_callback {
	int owner;
	void (*callback)(struct expression *call, int param, char *printed_name, struct smatch_state *state);
};
ALLOCATOR(member_info_callback, "caller_info callbacks");
DECLARE_PTR_LIST(member_info_cb_list, struct member_info_callback);
static struct member_info_cb_list *member_callbacks;

struct returned_state_callback {
	void (*callback)(int return_id, char *return_ranges, struct expression *return_expr);
};
ALLOCATOR(returned_state_callback, "returned state callbacks");
DECLARE_PTR_LIST(returned_state_cb_list, struct returned_state_callback);
static struct returned_state_cb_list *returned_state_callbacks;

struct returned_member_callback {
	int owner;
	void (*callback)(int return_id, char *return_ranges, struct expression *expr, char *printed_name, struct smatch_state *state);
};
ALLOCATOR(returned_member_callback, "returned member callbacks");
DECLARE_PTR_LIST(returned_member_cb_list, struct returned_member_callback);
static struct returned_member_cb_list *returned_member_callbacks;

struct call_implies_callback {
	int type;
	void (*callback)(struct expression *arg, char *key, char *value);
};
ALLOCATOR(call_implies_callback, "call_implies callbacks");
DECLARE_PTR_LIST(call_implies_cb_list, struct call_implies_callback);
static struct call_implies_cb_list *call_implies_cb_list;

static int print_sql_output(void *unused, int argc, char **argv, char **azColName)
{
	int i;

	for (i = 0; i < argc; i++) {
		if (i != 0)
			printf(", ");
		sm_printf("%s", argv[i]);
	}
	sm_printf("\n");
	return 0;
}

void debug_sql(const char *sql)
{
	if (!option_debug)
		return;
	sm_msg("%s", sql);
	sql_exec(print_sql_output, NULL, sql);

}

void sql_exec(int (*callback)(void*, int, char**, char**), void *data, const char *sql)
{
	char *err = NULL;
	int rc;

	if (option_no_db || !db)
		return;

	rc = sqlite3_exec(db, sql, callback, data, &err);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error #2: %s\n", err);
		fprintf(stderr, "SQL: '%s'\n", sql);
	}
}

void sql_mem_exec(int (*callback)(void*, int, char**, char**), void *data, const char *sql)
{
	char *err = NULL;
	int rc;

	if (!mem_db)
		return;

	rc = sqlite3_exec(mem_db, sql, callback, data, &err);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error #2: %s\n", err);
		fprintf(stderr, "SQL: '%s'\n", sql);
	}
}

void sql_insert_return_states(int return_id, const char *return_ranges,
		int type, int param, const char *key, const char *value)
{
	if (key && strlen(key) >= 80)
		return;
	sql_insert(return_states, "'%s', '%s', %lu, %d, '%s', %d, %d, %d, '%s', '%s'",
		   get_base_file(), get_function(), (unsigned long)__inline_fn,
		   return_id, return_ranges, fn_static(), type, param, key, value);
}

static struct string_list *common_funcs;
static int is_common_function(const char *fn)
{
	char *tmp;

	if (strncmp(fn, "__builtin_", 10) == 0)
		return 1;

	FOR_EACH_PTR(common_funcs, tmp) {
		if (strcmp(tmp, fn) == 0)
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

void sql_insert_caller_info(struct expression *call, int type,
		int param, const char *key, const char *value)
{
	char *fn;

	if (!option_info && !__inline_call)
		return;

	if (key && strlen(key) >= 80)
		return;

	fn = get_fnptr_name(call->fn);
	if (!fn)
		return;

	if (__inline_call) {
		mem_sql(NULL, NULL,
			"insert into caller_info values ('%s', '%s', '%s', %lu, %d, %d, %d, '%s', '%s');",
			get_base_file(), get_function(), fn, (unsigned long)call,
			is_static(call->fn), type, param, key, value);
	}

	if (!option_info)
		return;

	if (is_common_function(fn))
		return;

	sm_msg("SQL_caller_info: insert into caller_info values ("
	       "'%s', '%s', '%s', %%CALL_ID%%, %d, %d, %d, '%s', '%s');",
	       get_base_file(), get_function(), fn, is_static(call->fn),
	       type, param, key, value);

	free_string(fn);
}

void sql_insert_function_ptr(const char *fn, const char *struct_name)
{
	sql_insert(function_ptr, "'%s', '%s', '%s', 0", get_base_file(), fn,
		   struct_name);
}

void sql_insert_call_implies(int type, int param, const char *key, const char *value)
{
	sql_insert(call_implies, "'%s', '%s', %lu, %d, %d, %d, '%s', %s", get_base_file(),
	           get_function(), (unsigned long)__inline_fn, fn_static(),
		   type, param, key, value);
}

void sql_insert_function_type_size(const char *member, const char *ranges)
{
	sql_insert(function_type_size, "'%s', '%s', '%s', '%s'", get_base_file(), get_function(), member, ranges);
}

void sql_insert_local_values(const char *name, const char *value)
{
	sql_insert(local_values, "'%s', '%s', '%s'", get_base_file(), name, value);
}

void sql_insert_function_type_value(const char *type, const char *value)
{
	sql_insert(function_type_value, "'%s', '%s', '%s', '%s'", get_base_file(), get_function(), type, value);
}

void sql_insert_function_type_info(int param, const char *value)
{
	sql_insert(function_type_info, "'%s', '%s', %d, %d, '%s'",
		   get_base_file(), get_function(), fn_static(), param, value);
}

static char *get_static_filter(struct symbol *sym)
{
	static char sql_filter[1024];

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, sizeof(sql_filter),
			 "file = '%s' and function = '%s' and static = '1'",
			 get_base_file(), sym->ident->name);
	} else {
		snprintf(sql_filter, sizeof(sql_filter),
			 "function = '%s' and static = '0'", sym->ident->name);
	}

	return sql_filter;
}

static int row_count;
static int get_row_count(void *unused, int argc, char **argv, char **azColName)
{
	if (argc != 1)
		return 0;
	row_count = atoi(argv[0]);
	return 0;
}

static void sql_select_return_states_pointer(const char *cols,
	struct expression *call, int (*callback)(void*, int, char**, char**), void *info)
{
	char *ptr;

	ptr = get_fnptr_name(call->fn);
	if (!ptr)
		return;

	run_sql(callback, info,
		"select %s from return_states join function_ptr where "
		"return_states.function == function_ptr.function and ptr = '%s'"
		"and searchable = 1 order by return_id, type;",
		cols, ptr);
}

void sql_select_return_states(const char *cols, struct expression *call,
	int (*callback)(void*, int, char**, char**), void *info)
{
	if (is_fake_call(call))
		return;

	if (call->fn->type != EXPR_SYMBOL || !call->fn->symbol) {
		sql_select_return_states_pointer(cols, call, callback, info);
		return;
	}

	if (inlinable(call->fn)) {
		mem_sql(callback, info,
			"select %s from return_states where call_id = '%lu' order by return_id, type;",
			cols, (unsigned long)call);
		return;
	}

	row_count = 0;
	run_sql(get_row_count, info, "select count(*) from return_states where %s;",
		get_static_filter(call->fn->symbol));
	if (row_count > 1000)
		return;

	run_sql(callback, info, "select %s from return_states where %s order by return_id, type;",
		cols, get_static_filter(call->fn->symbol));
}

void sql_select_call_implies(const char *cols, struct expression *call,
	int (*callback)(void*, int, char**, char**))
{
	if (call->fn->type != EXPR_SYMBOL || !call->fn->symbol)
		return;

	if (inlinable(call->fn)) {
		mem_sql(callback, NULL,
			"select %s from call_implies where call_id = '%lu';",
			cols, (unsigned long)call);
		return;
	}

	run_sql(callback, NULL, "select %s from call_implies where %s;",
		cols, get_static_filter(call->fn->symbol));
}

void sql_select_caller_info(const char *cols, struct symbol *sym,
	int (*callback)(void*, int, char**, char**))
{
	if (__inline_fn) {
		mem_sql(callback, NULL,
			"select %s from caller_info where call_id = %lu;",
			cols, (unsigned long)__inline_fn);
		return;
	}

	run_sql(callback, NULL,
		"select %s from caller_info where %s order by call_id;",
		cols, get_static_filter(sym));
}

void select_caller_info_hook(void (*callback)(const char *name, struct symbol *sym, char *key, char *value), int type)
{
	struct def_callback *def_callback = __alloc_def_callback(0);

	def_callback->hook_type = type;
	def_callback->callback = callback;
	add_ptr_list(&callbacks, def_callback);
}

/*
 * These call backs are used when the --info option is turned on to print struct
 * member information.  For example foo->bar could have a state in
 * smatch_extra.c and also check_user.c.
 */
void add_member_info_callback(int owner, void (*callback)(struct expression *call, int param, char *printed_name, struct smatch_state *state))
{
	struct member_info_callback *member_callback = __alloc_member_info_callback(0);

	member_callback->owner = owner;
	member_callback->callback = callback;
	add_ptr_list(&member_callbacks, member_callback);
}

void add_split_return_callback(void (*fn)(int return_id, char *return_ranges, struct expression *returned_expr))
{
	struct returned_state_callback *callback = __alloc_returned_state_callback(0);

	callback->callback = fn;
	add_ptr_list(&returned_state_callbacks, callback);
}

void add_returned_member_callback(int owner, void (*callback)(int return_id, char *return_ranges, struct expression *expr, char *printed_name, struct smatch_state *state))
{
	struct returned_member_callback *member_callback = __alloc_returned_member_callback(0);

	member_callback->owner = owner;
	member_callback->callback = callback;
	add_ptr_list(&returned_member_callbacks, member_callback);
}

void select_call_implies_hook(int type, void (*callback)(struct expression *arg, char *key, char *value))
{
	struct call_implies_callback *cb = __alloc_call_implies_callback(0);

	cb->type = type;
	cb->callback = callback;
	add_ptr_list(&call_implies_cb_list, cb);
}

static struct expression *static_call_expr;
static struct expression *static_returns_call;
static struct symbol *return_type;
static struct range_list *return_range_list;
static int db_return_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct range_list *rl;
	struct expression *call_expr = static_returns_call;

	if (argc != 1)
		return 0;
	call_results_to_rl(call_expr, return_type, argv[0], &rl);
	return_range_list = rl_union(return_range_list, rl);
	return 0;
}

struct range_list *db_return_vals(struct expression *expr)
{
	static_returns_call = expr;
	return_type = get_type(expr);
	if (!return_type)
		return NULL;
	if (is_fake_call(expr))
		return NULL;
	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return NULL;

	return_range_list = NULL;
	if (inlinable(expr->fn)) {
		mem_sql(db_return_callback, NULL,
			"select distinct return from return_states where call_id = '%lu';",
			(unsigned long)expr);
	} else {
		run_sql(db_return_callback, NULL,
			"select distinct return from return_states where %s;",
			get_static_filter(expr->fn->symbol));
	}
	return return_range_list;
}

static void match_call_marker(struct expression *expr)
{
	/*
	 * we just want to record something in the database so that if we have
	 * two calls like:  frob(4); frob(some_unkown); then on the receiving
	 * side we know that sometimes frob is called with unknown parameters.
	 */

	sql_insert_caller_info(expr, INTERNAL, -1, "%call_marker%", "");
}

static void print_struct_members(struct expression *call, struct expression *expr, int param, struct stree *stree,
	void (*callback)(struct expression *call, int param, char *printed_name, struct smatch_state *state))
{
	struct sm_state *sm;
	char *name;
	struct symbol *sym;
	int len;
	char printed_name[256];
	int is_address = 0;

	expr = strip_expr(expr);
	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		is_address = 1;
	}

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	len = strlen(name);
	FOR_EACH_SM(stree, sm) {
		if (sm->sym != sym)
			continue;
		if (strcmp(name, sm->name) == 0) {
			if (is_address)
				snprintf(printed_name, sizeof(printed_name), "*$$");
			else /* these are already handled. fixme: handle them here */
				continue;
		} else if (sm->name[0] == '*' && strcmp(name, sm->name + 1) == 0) {
			snprintf(printed_name, sizeof(printed_name), "*$$");
		} else if (strncmp(name, sm->name, len) == 0) {
			if (is_address)
				snprintf(printed_name, sizeof(printed_name), "$$->%s", sm->name + len + 1);
			else
				snprintf(printed_name, sizeof(printed_name), "$$%s", sm->name + len);
		} else {
			continue;
		}
		callback(call, param, printed_name, sm->state);
	} END_FOR_EACH_SM(sm);
free:
	free_string(name);
}

static void match_call_info(struct expression *call)
{
	struct member_info_callback *cb;
	struct expression *arg;
	struct stree *stree;
	char *name;
	int i;

	name = get_fnptr_name(call->fn);
	if (!name)
		return;

	FOR_EACH_PTR(member_callbacks, cb) {
		stree = get_all_states_stree(cb->owner);
		i = 0;
		FOR_EACH_PTR(call->args, arg) {
			print_struct_members(call, arg, i, stree, cb->callback);
			i++;
		} END_FOR_EACH_PTR(arg);
		free_stree(&stree);
	} END_FOR_EACH_PTR(cb);

	free_string(name);
}

static int get_param(int param, char **name, struct symbol **sym)
{
	struct symbol *arg;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		/*
		 * this is a temporary hack to work around a bug (I think in sparse?)
		 * 2.6.37-rc1:fs/reiserfs/journal.o
		 * If there is a function definition without parameter name found
		 * after a function implementation then it causes a crash.
		 * int foo() {}
		 * int bar(char *);
		 */
		if (arg->ident->name < (char *)100)
			continue;
		if (i == param) {
			*name = arg->ident->name;
			*sym = arg;
			return TRUE;
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	return FALSE;
}

static struct stree *final_states;
static int prev_func_id = -1;
static int caller_info_callback(void *unused, int argc, char **argv, char **azColName)
{
	int func_id;
	long type;
	long param;
	char *name = NULL;
	struct symbol *sym = NULL;
	struct def_callback *def_callback;
	struct stree *stree;

	if (argc != 5)
		return 0;

	func_id = atoi(argv[0]);
	errno = 0;
	type = strtol(argv[1], NULL, 10);
	param = strtol(argv[2], NULL, 10);
	if (errno)
		return 0;

	if (prev_func_id == -1)
		prev_func_id = func_id;
	if (func_id != prev_func_id) {
		stree = __pop_fake_cur_stree();
		merge_stree(&final_states, stree);
		free_stree(&stree);
		__push_fake_cur_stree();
		__unnullify_path();
		prev_func_id = func_id;
	}

	if (type == INTERNAL)
		return 0;
	if (param >= 0 && !get_param(param, &name, &sym))
		return 0;

	FOR_EACH_PTR(callbacks, def_callback) {
		if (def_callback->hook_type == type)
			def_callback->callback(name, sym, argv[3], argv[4]);
	} END_FOR_EACH_PTR(def_callback);

	return 0;
}

static void get_direct_callers(struct symbol *sym)
{
	sql_select_caller_info("call_id, type, parameter, key, value", sym,
			caller_info_callback);
}

static struct string_list *ptr_names_done;
static struct string_list *ptr_names;

static int get_ptr_name(void *unused, int argc, char **argv, char **azColName)
{
	insert_string(&ptr_names, alloc_string(argv[0]));
	return 0;
}

static char *get_next_ptr_name(void)
{
	char *ptr;

	FOR_EACH_PTR(ptr_names, ptr) {
		if (list_has_string(ptr_names_done, ptr))
			continue;
		insert_string(&ptr_names_done, ptr);
		return ptr;
	} END_FOR_EACH_PTR(ptr);
	return NULL;
}

static void get_ptr_names(const char *file, const char *name)
{
	char sql_filter[1024];
	int before, after;

	if (file) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 file, name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s';", name);
	}

	before = ptr_list_size((struct ptr_list *)ptr_names);

	run_sql(get_ptr_name, NULL,
		"select distinct ptr from function_ptr where %s",
		sql_filter);

	after = ptr_list_size((struct ptr_list *)ptr_names);
	if (before == after)
		return;

	while ((name = get_next_ptr_name()))
		get_ptr_names(NULL, name);
}

static void match_data_from_db(struct symbol *sym)
{
	struct sm_state *sm;
	struct stree *stree;

	if (!sym || !sym->ident)
		return;

	__push_fake_cur_stree();
	__unnullify_path();
	prev_func_id = -1;

	if (!__inline_fn) {
		char *ptr;

		if (sym->ctype.modifiers & MOD_STATIC)
			get_ptr_names(get_base_file(), sym->ident->name);
		else
			get_ptr_names(NULL, sym->ident->name);

		if (ptr_list_size((struct ptr_list *)ptr_names) > 20) {
			__free_ptr_list((struct ptr_list **)&ptr_names);
			__free_ptr_list((struct ptr_list **)&ptr_names_done);
			stree = __pop_fake_cur_stree();
			free_stree(&stree);
			return;
		}

		get_direct_callers(sym);

		FOR_EACH_PTR(ptr_names, ptr) {
			run_sql(caller_info_callback, NULL,
				"select call_id, type, parameter, key, value"
				" from caller_info where function = '%s' order by call_id",
				ptr);
			free_string(ptr);
		} END_FOR_EACH_PTR(ptr);

		__free_ptr_list((struct ptr_list **)&ptr_names);
		__free_ptr_list((struct ptr_list **)&ptr_names_done);
	} else {
		get_direct_callers(sym);
	}

	stree = __pop_fake_cur_stree();
	merge_stree(&final_states, stree);
	free_stree(&stree);

	FOR_EACH_SM(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_SM(sm);

	free_stree(&final_states);
}

static int call_implies_callbacks(void *unused, int argc, char **argv, char **azColName)
{
	struct expression *call_expr = static_call_expr;
	struct call_implies_callback *cb;
	struct expression *arg = NULL;
	int type;
	int param;

	if (argc != 5)
		return 0;

	type = atoi(argv[1]);
	param = atoi(argv[2]);

	FOR_EACH_PTR(call_implies_cb_list, cb) {
		if (cb->type != type)
			continue;
		if (param != -1) {
			arg = get_argument_from_call_expr(call_expr->args, param);
			if (!arg)
				continue;
		}
		cb->callback(arg, argv[3], argv[4]);
	} END_FOR_EACH_PTR(cb);

	return 0;
}

static void match_call_implies(struct expression *expr)
{
	static_call_expr = expr;
	sql_select_call_implies("function, type, parameter, key, value", expr,
				call_implies_callbacks);
	return;
}

static void print_initializer_list(struct expression_list *expr_list,
		struct symbol *struct_type)
{
	struct expression *expr;
	struct symbol *base_type;
	char struct_name[256];

	FOR_EACH_PTR(expr_list, expr) {
		if (expr->type == EXPR_INDEX && expr->idx_expression && expr->idx_expression->type == EXPR_INITIALIZER) {
			print_initializer_list(expr->idx_expression->expr_list, struct_type);
			continue;
		}
		if (expr->type != EXPR_IDENTIFIER)
			continue;
		if (!expr->expr_ident)
			continue;
		if (!expr->ident_expression || !expr->ident_expression->symbol_name)
			continue;
		base_type = get_type(expr->ident_expression);
		if (!base_type || base_type->type != SYM_FN)
			continue;
		snprintf(struct_name, sizeof(struct_name), "(struct %s)->%s",
			 struct_type->ident->name, expr->expr_ident->name);
		sql_insert_function_ptr(expr->ident_expression->symbol_name->name,
				        struct_name);
	} END_FOR_EACH_PTR(expr);
}

static void global_variable(struct symbol *sym)
{
	struct symbol *struct_type;

	if (!sym->ident)
		return;
	if (!sym->initializer || sym->initializer->type != EXPR_INITIALIZER)
		return;
	struct_type = get_base_type(sym);
	if (!struct_type)
		return;
	if (struct_type->type == SYM_ARRAY) {
		struct_type = get_base_type(struct_type);
		if (!struct_type)
			return;
	}
	if (struct_type->type != SYM_STRUCT || !struct_type->ident)
		return;
	print_initializer_list(sym->initializer->expr_list, struct_type);
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	sql_insert_return_states(return_id, return_ranges, INTERNAL, -1, "", "");
}

static void call_return_state_hooks_conditional(struct expression *expr)
{
	struct returned_state_callback *cb;
	struct range_list *rl;
	char *return_ranges;
	int final_pass_orig = final_pass;

	__push_fake_cur_stree();

	final_pass = 0;
	__split_whole_condition(expr->conditional);
	final_pass = final_pass_orig;

	if (get_implied_rl(expr->cond_true, &rl))
		rl = cast_rl(cur_func_return_type(), rl);
	else
		rl = cast_rl(cur_func_return_type(), alloc_whole_rl(get_type(expr->cond_true)));
	return_ranges = show_rl(rl);

	return_id++;
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr);
	} END_FOR_EACH_PTR(cb);

	__push_true_states();
	__use_false_states();

	if (get_implied_rl(expr->cond_false, &rl))
		rl = cast_rl(cur_func_return_type(), rl);
	else
		rl = cast_rl(cur_func_return_type(), alloc_whole_rl(get_type(expr->cond_false)));
	return_ranges = show_rl(rl);

	return_id++;
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr);
	} END_FOR_EACH_PTR(cb);

	__merge_true_states();
	__free_fake_cur_stree();
}

static void call_return_state_hooks_compare(struct expression *expr)
{
	struct returned_state_callback *cb;
	char *return_ranges;
	int final_pass_orig = final_pass;

	__push_fake_cur_stree();

	final_pass = 0;
	__split_whole_condition(expr);
	final_pass = final_pass_orig;

	return_ranges = alloc_sname("1");

	return_id++;
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr);
	} END_FOR_EACH_PTR(cb);

	__push_true_states();
	__use_false_states();

	return_ranges = alloc_sname("0");;
	return_id++;
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr);
	} END_FOR_EACH_PTR(cb);

	__merge_true_states();
	__free_fake_cur_stree();
}

static int call_return_state_hooks_split_possible(struct expression *expr)
{
	struct returned_state_callback *cb;
	struct range_list *rl;
	char *return_ranges;
	struct sm_state *sm;
	struct sm_state *tmp;
	int ret = 0;
	int nr_possible, nr_states;
	char *compare_str;
	char buf[128];

	if (!expr || expr_equal_to_param(expr, -1))
		return 0;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm || !sm->merged)
		return 0;

	if (too_many_possible(sm))
		return 0;

	/* bail if it gets too complicated */
	nr_possible = ptr_list_size((struct ptr_list *)sm->possible);
	nr_states = stree_count(__get_cur_stree());
	/*
	 * the main thing option_info because we don't want to print a
	 * million lines of output.  If someone else, like check_locking.c
	 * wants this data, then it doesn't cause a slow down to provide it.
	 */
	if (option_info && nr_states * nr_possible >= 2000)
		return 0;

	compare_str = expr_lte_to_param(expr, -1);

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->merged)
			continue;

		ret = 1;
		__push_fake_cur_stree();

		overwrite_states_using_pool(tmp);

		rl = cast_rl(cur_func_return_type(), estate_rl(tmp->state));
		return_ranges = show_rl(rl);
		if (compare_str) {
			snprintf(buf, sizeof(buf), "%s%s", return_ranges, compare_str);
			return_ranges = alloc_sname(buf);
		}

		return_id++;
		FOR_EACH_PTR(returned_state_callbacks, cb) {
			cb->callback(return_id, return_ranges, expr);
		} END_FOR_EACH_PTR(cb);

		__free_fake_cur_stree();
	} END_FOR_EACH_PTR(tmp);

	return ret;
}

static const char *get_return_ranges_str(struct expression *expr)
{
	struct range_list *rl;
	char *return_ranges;
	sval_t sval;
	char *compare_str;
	char *math_str;
	char buf[128];

	if (!expr)
		return alloc_sname("");
	compare_str = expr_equal_to_param(expr, -1);
	if (compare_str)
		return compare_str;

	if (get_implied_value(expr, &sval))
		return sval_to_str(sval);

	math_str = get_value_in_terms_of_parameter_math(expr);

	if (get_implied_rl(expr, &rl)) {
		rl = cast_rl(cur_func_return_type(), rl);
		return_ranges = show_rl(rl);
	} else {
		rl = cast_rl(cur_func_return_type(), alloc_whole_rl(get_type(expr)));
		return_ranges = show_rl(rl);
	}

	if (math_str) {
		snprintf(buf, sizeof(buf), "%s[%s]", return_ranges, math_str);
		return alloc_sname(buf);
	}

	compare_str = expr_lte_to_param(expr, -1);
	if (compare_str) {
		snprintf(buf, sizeof(buf), "%s%s", return_ranges, compare_str);
		return alloc_sname(buf);
	}
	return return_ranges;
}

static int is_boolean(struct expression *expr)
{
	struct range_list *rl;

	if (!get_implied_rl(expr, &rl))
		return 0;
	if (rl_min(rl).value == 0 && rl_max(rl).value == 1)
		return 1;
	return 0;
}

static int is_conditional(struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->type == EXPR_CONDITIONAL || expr->type == EXPR_SELECT)
		return 1;
	return 0;
}

static void call_return_state_hooks(struct expression *expr)
{
	struct returned_state_callback *cb;
	const char *return_ranges;
	int nr_states;

	expr = strip_expr(expr);

	if (is_condition(expr) || is_boolean(expr)) {
		call_return_state_hooks_compare(expr);
		return;
	} else if (is_conditional(expr)) {
		call_return_state_hooks_conditional(expr);
		return;
	} else if (call_return_state_hooks_split_possible(expr)) {
		return;
	}

	return_ranges = get_return_ranges_str(expr);

	return_id++;
	nr_states = stree_count(__get_cur_stree());
	if (nr_states >= 10000) {
		match_return_info(return_id, (char *)return_ranges, expr);
		return;
	}
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, (char *)return_ranges, expr);
	} END_FOR_EACH_PTR(cb);
}

static void print_returned_struct_members(int return_id, char *return_ranges, struct expression *expr)
{
	struct returned_member_callback *cb;
	struct stree *stree;
	struct sm_state *sm;
	struct symbol *type;
	char *name;
	char member_name[256];
	int len;

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return;
	name = expr_to_var(expr);
	if (!name)
		return;

	member_name[sizeof(member_name) - 1] = '\0';
	strcpy(member_name, "$$");

	len = strlen(name);
	FOR_EACH_PTR(returned_member_callbacks, cb) {
		stree = __get_cur_stree();
		FOR_EACH_MY_SM(cb->owner, stree, sm) {
			if (sm->name[0] == '*' && strcmp(sm->name + 1, name) == 0) {
				strcpy(member_name, "*$$");
				cb->callback(return_id, return_ranges, expr, member_name, sm->state);
				continue;
			}
			if (strncmp(sm->name, name, len) != 0)
				continue;
			if (strncmp(sm->name + len, "->", 2) != 0)
				continue;
			strcpy(member_name, "$$");
			strncpy(member_name + 2, sm->name + len, sizeof(member_name) - 2);
			cb->callback(return_id, return_ranges, expr, member_name, sm->state);
		} END_FOR_EACH_SM(sm);
	} END_FOR_EACH_PTR(cb);

	free_string(name);
}

static void reset_memdb(void)
{
	mem_sql(NULL, NULL, "delete from caller_info;");
	mem_sql(NULL, NULL, "delete from return_states;");
	mem_sql(NULL, NULL, "delete from call_implies;");
}

static void match_end_func_info(struct symbol *sym)
{
	if (__path_is_null())
		return;
	call_return_state_hooks(NULL);
	if (!__inline_fn)
		reset_memdb();
}

static void init_memdb(void)
{
	char *err = NULL;
	int rc;
	const char *schema_files[] = {
		"db/db.schema",
		"db/caller_info.schema",
		"db/return_states.schema",
		"db/function_type_size.schema",
		"db/type_size.schema",
		"db/call_implies.schema",
		"db/function_ptr.schema",
		"db/local_values.schema",
		"db/function_type_value.schema",
		"db/type_value.schema",
		"db/function_type_info.schema",
	};
	static char buf[4096];
	int fd;
	int ret;
	int i;

	rc = sqlite3_open(":memory:", &mem_db);
	if (rc != SQLITE_OK) {
		printf("Error starting In-Memory database.");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(schema_files); i++) {
		fd = open_data_file(schema_files[i]);
		if (fd < 0) {
			mem_db = NULL;
			return;
		}
		ret = read(fd, buf, sizeof(buf));
		if (ret == sizeof(buf)) {
			printf("Schema file too large:  %s (limit %zd bytes)",
			       schema_files[i], sizeof(buf));
		}
		buf[ret] = '\0';
		rc = sqlite3_exec(mem_db, buf, NULL, 0, &err);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "SQL error #2: %s\n", err);
			fprintf(stderr, "%s\n", buf);
		}
	}
}

void open_smatch_db(void)
{
	int rc;

	if (option_no_db)
		return;

	init_memdb();

	rc = sqlite3_open_v2("smatch_db.sqlite", &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		option_no_db = 1;
		return;
	}
	return;
}

static void register_common_funcs(void)
{
	struct token *token;
	char *func;
	char filename[256];

	if (option_project == PROJ_NONE)
		strcpy(filename, "common_functions");
	else
		snprintf(filename, 256, "%s.common_functions", option_project_str);

	token = get_tokens_file(filename);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = alloc_string(show_ident(token->ident));
		add_ptr_list(&common_funcs, func);
		token = token->next;
	}
	clear_token_alloc();
}


void register_definition_db_callbacks(int id)
{
	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_hook(&global_variable, BASE_HOOK);
	add_hook(&global_variable, DECLARATION_HOOK);
	add_split_return_callback(match_return_info);
	add_split_return_callback(print_returned_struct_members);
	add_hook(&call_return_state_hooks, RETURN_HOOK);
	add_hook(&match_end_func_info, END_FUNC_HOOK);

	add_hook(&match_data_from_db, FUNC_DEF_HOOK);
	add_hook(&match_call_implies, CALL_HOOK_AFTER_INLINE);

	register_common_funcs();
}

void register_db_call_marker(int id)
{
	add_hook(&match_call_marker, FUNCTION_CALL_HOOK);
}

char *return_state_to_var_sym(struct expression *expr, int param, char *key, struct symbol **sym)
{
	struct expression *arg;
	char *name = NULL;
	char member_name[256];

	*sym = NULL;

	if (param == -1) {
		const char *star = "";

		if (expr->type != EXPR_ASSIGNMENT)
			return NULL;
		name = expr_to_var_sym(expr->left, sym);
		if (!name)
			return NULL;
		if (key[0] == '*') {
			star = "*";
			key++;
		}
		if (strncmp(key, "$$", 2) != 0)
			return name;
		snprintf(member_name, sizeof(member_name), "%s%s%s", star, name, key + 2);
		free_string(name);
		return alloc_string(member_name);
	}

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return NULL;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return NULL;

	return get_variable_from_key(arg, key, sym);
}

char *get_variable_from_key(struct expression *arg, char *key, struct symbol **sym)
{
	char buf[256];
	char *tmp;

	if (strcmp(key, "$$") == 0)
		return expr_to_var_sym(arg, sym);

	if (strcmp(key, "*$$") == 0) {
		if (arg->type == EXPR_PREOP && arg->op == '&') {
			arg = strip_expr(arg->unop);
			return expr_to_var_sym(arg, sym);
		} else {
			tmp = expr_to_var_sym(arg, sym);
			if (!tmp)
				return NULL;
			snprintf(buf, sizeof(buf), "*%s", tmp);
			free_string(tmp);
			return alloc_string(buf);
		}
	}

	if (arg->type == EXPR_PREOP && arg->op == '&') {
		arg = strip_expr(arg->unop);
		tmp = expr_to_var_sym(arg, sym);
		if (!tmp)
			return NULL;
		snprintf(buf, sizeof(buf), "%s.%s", tmp, key + 4);
		return alloc_string(buf);
	}

	tmp = expr_to_var_sym(arg, sym);
	if (!tmp)
		return NULL;
	snprintf(buf, sizeof(buf), "%s%s", tmp, key + 2);
	free_string(tmp);
	return alloc_string(buf);
}

const char *get_param_name(struct sm_state *sm)
{
	char *param_name;
	int name_len;
	static char buf[256];

	if (!sm->sym->ident)
		return NULL;

	param_name = sm->sym->ident->name;
	name_len = strlen(param_name);

	if (strcmp(sm->name, param_name) == 0) {
		return "$$";
	} else if (sm->name[name_len] == '-' && /* check for '-' from "->" */
	    strncmp(sm->name, param_name, name_len) == 0) {
		snprintf(buf, sizeof(buf), "$$%s", sm->name + name_len);
		return buf;
	} else if (sm->name[0] == '*' && strcmp(sm->name + 1, param_name) == 0) {
		return "*$$";
	}
	return NULL;
}

