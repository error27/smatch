/*
 * smatch/smatch_db.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <string.h>
#include <errno.h>
#include <sqlite3.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static sqlite3 *db;

struct def_callback {
	int hook_type;
	void (*callback)(const char *name, struct symbol *sym, char *key, char *value);
};
ALLOCATOR(def_callback, "definition db hook callbacks");
DECLARE_PTR_LIST(callback_list, struct def_callback);
static struct callback_list *callbacks;

struct member_info_callback {
	int owner;
	void (*callback)(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state);
};
ALLOCATOR(member_info_callback, "caller_info callbacks");
DECLARE_PTR_LIST(member_info_cb_list, struct member_info_callback);
static struct member_info_cb_list *member_callbacks;

struct returned_state_callback {
	void (*callback)(int return_id, char *return_ranges, struct expression *return_expr, struct state_list *slist);
};
ALLOCATOR(returned_state_callback, "returned state callbacks");
DECLARE_PTR_LIST(returned_state_cb_list, struct returned_state_callback);
static struct returned_state_cb_list *returned_state_callbacks;

struct returned_member_callback {
	int owner;
	void (*callback)(int return_id, char *return_ranges, char *printed_name, struct smatch_state *state);
};
ALLOCATOR(returned_member_callback, "returned member callbacks");
DECLARE_PTR_LIST(returned_member_cb_list, struct returned_member_callback);
static struct returned_member_cb_list *returned_member_callbacks;

struct call_implies_callback {
	int type;
	void (*callback)(struct expression *arg, char *value);
};
ALLOCATOR(call_implies_callback, "call_implies callbacks");
DECLARE_PTR_LIST(call_implies_cb_list, struct call_implies_callback);
static struct call_implies_cb_list *call_implies_cb_list;

void sql_exec(int (*callback)(void*, int, char**, char**), const char *sql)
{
	char *err = NULL;
	int rc;

	if (option_no_db || !db)
		return;

	rc = sqlite3_exec(db, sql, callback, 0, &err);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error #2: %s\n", err);
		fprintf(stderr, "SQL: '%s'\n", sql);
	}
}

void add_definition_db_callback(void (*callback)(const char *name, struct symbol *sym, char *key, char *value), int type)
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
void add_member_info_callback(int owner, void (*callback)(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state))
{
	struct member_info_callback *member_callback = __alloc_member_info_callback(0);

	member_callback->owner = owner;
	member_callback->callback = callback;
	add_ptr_list(&member_callbacks, member_callback);
}

void add_returned_state_callback(void (*fn)(int return_id, char *return_ranges, struct expression *returned_expr, struct state_list *slist))
{
	struct returned_state_callback *callback = __alloc_returned_state_callback(0);

	callback->callback = fn;
	add_ptr_list(&returned_state_callbacks, callback);
}

void add_returned_member_callback(int owner, void (*callback)(int return_id, char *return_ranges, char *printed_name, struct smatch_state *state))
{
	struct returned_member_callback *member_callback = __alloc_returned_member_callback(0);

	member_callback->owner = owner;
	member_callback->callback = callback;
	add_ptr_list(&returned_member_callbacks, member_callback);
}

void add_db_fn_call_callback(int type, void (*callback)(struct expression *arg, char *value))
{
	struct call_implies_callback *cb = __alloc_call_implies_callback(0);

	cb->type = type;
	cb->callback = callback;
	add_ptr_list(&call_implies_cb_list, cb);
}

static struct symbol *return_type;
static struct range_list *return_range_list;
static int db_return_callback(void *unused, int argc, char **argv, char **azColName)
{
	if (argc != 1)
		return 0;
	if (option_debug)
		sm_msg("return type %d", type_positive_bits(return_type));
	str_to_rl(return_type, argv[0], &return_range_list);
	return 0;
}

struct range_list *db_return_vals(struct expression *expr)
{
	struct symbol *sym;
	static char sql_filter[1024];

	if (expr->type != EXPR_CALL)
		return NULL;
	if (expr->fn->type != EXPR_SYMBOL)
		return NULL;
	return_type = get_type(expr);
	if (!return_type)
		return NULL;
	sym = expr->fn->symbol;
	if (!sym)
		return NULL;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s' and static = 0;",
				sym->ident->name);
	}

	return_range_list = NULL;
	run_sql(db_return_callback, "select return from return_values where %s",
		 sql_filter);
	return return_range_list;
}

static void match_call_hack(struct expression *expr)
{
	char *name;

	/*
	 * we just want to record something in the database so that if we have
	 * two calls like:  frob(4); frob(some_unkown); then on the receiving
	 * side we know that sometimes frob is called with unknown parameters.
	 */

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;
	sm_msg("info: call_marker '%s' %s", name, is_static(expr->fn) ? "static" : "global");
	free_string(name);
}

static void print_struct_members(char *fn, char *global_static, struct expression *expr, int param, struct state_list *slist,
	void (*callback)(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state))
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
	FOR_EACH_PTR(slist, sm) {
		if (sm->sym != sym)
			continue;
		if (strncmp(name, sm->name, len) || sm->name[len] == '\0')
			continue;
		if (is_address)
			snprintf(printed_name, sizeof(printed_name), "$$->%s", sm->name + len + 1);
		else
			snprintf(printed_name, sizeof(printed_name), "$$%s", sm->name + len);
		callback(fn, global_static, param, printed_name, sm->state);
	} END_FOR_EACH_PTR(sm);
free:
	free_string(name);
}

static void match_call_info(struct expression *expr)
{
	struct member_info_callback *cb;
	struct expression *arg;
	struct state_list *slist;
	char *name;
	int i;
	char *gs;

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;

	if (is_static(expr->fn))
		gs = (char *)"static";
	else
		gs = (char *)"global";

	FOR_EACH_PTR(member_callbacks, cb) {
		slist = get_all_states(cb->owner);
		i = 0;
		FOR_EACH_PTR(expr->args, arg) {
			print_struct_members(name, gs, arg, i, slist, cb->callback);
			i++;
		} END_FOR_EACH_PTR(arg);
		free_slist(&slist);
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
		if (i == param && arg->ident->name) {
			*name = arg->ident->name;
			*sym = arg;
			return TRUE;
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	return FALSE;
}

static struct state_list *final_states;
static int prev_func_id = -1;
static int db_callback(void *unused, int argc, char **argv, char **azColName)
{
	int func_id;
	long type;
	long param;
	char *name = NULL;
	struct symbol *sym = NULL;
	struct def_callback *def_callback;

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
		merge_slist(&final_states, __pop_fake_cur_slist());
		__push_fake_cur_slist();
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
	char sql_filter[1024];

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024,
			 "file = '%s' and function = '%s' order by function_id;",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024,
			 "function = '%s' and static = 0 order by function_id;",
			 sym->ident->name);
	}

	run_sql(db_callback, "select function_id, type, parameter, key, value from caller_info"
		" where %s", sql_filter);
}

static char *ptr_name;
static int get_ptr_name(void *unused, int argc, char **argv, char **azColName)
{
	if (!ptr_name)
		ptr_name = alloc_string(argv[0]);
	return 0;
}

static void get_function_pointer_callers(struct symbol *sym)
{
	ptr_name = NULL;
	run_sql(get_ptr_name, "select ptr from function_ptr where function = '%s'",
		sym->ident->name);
	if (!ptr_name)
		return;

	run_sql(db_callback, "select function_id, type, parameter, key, value from caller_info"
		" where function = '%s' order by function_id", ptr_name);

	free_string(ptr_name);
}

static void match_data_from_db(struct symbol *sym)
{
	struct sm_state *sm;

	if (!sym || !sym->ident || !sym->ident->name)
		return;

	__push_fake_cur_slist();
	__unnullify_path();
	prev_func_id = -1;

	get_direct_callers(sym);
	get_function_pointer_callers(sym);

	merge_slist(&final_states, __pop_fake_cur_slist());

	FOR_EACH_PTR(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&final_states);
}

static void match_function_assign(struct expression *expr)
{
	struct expression *right = expr->right;
	struct symbol *sym;
	char *fn_name;
	char *ptr_name;

	if (right->type == EXPR_PREOP && right->op == '&')
		right = strip_expr(right->unop);
	if (right->type != EXPR_SYMBOL)
		return;
	sym = get_type(right);
	if (!sym || sym->type != SYM_FN)
		return;

	fn_name = expr_to_var(right);
	ptr_name = get_fnptr_name(expr->left);
	if (!fn_name || !ptr_name)
		goto free;

	sm_msg("info: sets_fn_ptr '%s' '%s'", ptr_name, fn_name);

free:
	free_string(fn_name);
	free_string(ptr_name);
}

static struct expression *call_implies_call_expr;
static int call_implies_callbacks(void *unused, int argc, char **argv, char **azColName)
{
	struct call_implies_callback *cb;
	struct expression *arg = NULL;
	int type;
	int param;

	if (argc != 4)
		return 0;

	type = atoi(argv[1]);
	param = atoi(argv[2]);

	FOR_EACH_PTR(call_implies_cb_list, cb) {
		if (cb->type != type)
			continue;
		if (param != -1) {
			arg = get_argument_from_call_expr(call_implies_call_expr->args, param);
			if (!arg)
				continue;
		}
		cb->callback(arg, argv[3]);
	} END_FOR_EACH_PTR(cb);

	return 0;
}

static void match_call_implies(struct expression *expr)
{
	struct symbol *sym;
	static char sql_filter[1024];

	if (expr->fn->type != EXPR_SYMBOL)
		return;
	sym = expr->fn->symbol;
	if (!sym)
		return;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s' and static = 0;",
				sym->ident->name);
	}

	call_implies_call_expr = expr;
	run_sql(call_implies_callbacks,
		"select function, type, parameter, value from call_implies where %s",
		sql_filter);
	return;
}

static void print_initializer_list(struct expression_list *expr_list,
		struct symbol *struct_type)
{
	struct expression *expr;
	struct symbol *base_type;

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
		sm_msg("info: sets_fn_ptr '(struct %s)->%s' '%s'", struct_type->ident->name,
		       expr->expr_ident->name,
		       expr->ident_expression->symbol_name->name);
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

static void match_return_info(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	sm_msg("info: return_marker %d '%s' %s", return_id, return_ranges,
	       global_static());
}

static int return_id;
static void match_function_def(struct symbol *sym)
{
	return_id = 0;
}

static void call_return_state_hooks_compare(struct expression *expr)
{
	struct returned_state_callback *cb;
	struct state_list *slist;
	char *return_ranges;
	int final_pass_orig = final_pass;

	__push_fake_cur_slist();

	final_pass = 0;
	__split_whole_condition(expr);
	final_pass = final_pass_orig;

	return_ranges = alloc_sname("1");

	return_id++;
	slist = __get_cur_slist();
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr, slist);
	} END_FOR_EACH_PTR(cb);

	__push_true_states();
	__use_false_states();

	return_ranges = alloc_sname("0");;
	return_id++;
	slist = __get_cur_slist();
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr, slist);
	} END_FOR_EACH_PTR(cb);

	__merge_true_states();
	__pop_fake_cur_slist();
}

static int call_return_state_hooks_split_possible(struct expression *expr)
{
	struct returned_state_callback *cb;
	struct state_list *slist;
	struct range_list *rl;
	char *return_ranges;
	struct sm_state *sm;
	struct sm_state *tmp;
	int ret = 0;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm || !sm->merged)
		return 0;

	/* bail if it gets too complicated */
	if (ptr_list_size((struct ptr_list *)sm->possible) >= 100)
		return 0;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (tmp->merged)
			continue;

		ret = 1;
		__push_fake_cur_slist();

		overwrite_states_using_pool(tmp);

		rl = cast_rl(cur_func_return_type(), estate_rl(tmp->state));
		return_ranges = show_rl(rl);

		return_id++;
		slist = __get_cur_slist();
		FOR_EACH_PTR(returned_state_callbacks, cb) {
			cb->callback(return_id, return_ranges, expr, slist);
		} END_FOR_EACH_PTR(cb);

		__pop_fake_cur_slist();
	} END_FOR_EACH_PTR(tmp);

	return ret;
}

static void call_return_state_hooks(struct expression *expr)
{
	struct returned_state_callback *cb;
	struct state_list *slist;
	struct range_list *rl;
	char *return_ranges;

	expr = strip_expr(expr);

	if (!expr) {
		return_ranges = alloc_sname("");
	} else if (is_condition(expr)) {
		call_return_state_hooks_compare(expr);
		return;
	} else if (call_return_state_hooks_split_possible(expr)) {
		return;
	} else if (get_implied_rl(expr, &rl)) {
		rl = cast_rl(cur_func_return_type(), rl);
		return_ranges = show_rl(rl);
	} else {
		rl = alloc_whole_rl(cur_func_return_type());
		return_ranges = show_rl(rl);
	}

	return_id++;
	slist = __get_cur_slist();
	FOR_EACH_PTR(returned_state_callbacks, cb) {
		cb->callback(return_id, return_ranges, expr, slist);
	} END_FOR_EACH_PTR(cb);
}

static void print_returned_struct_members(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct returned_member_callback *cb;
	struct state_list *my_slist;
	struct sm_state *sm;
	struct symbol *type;
	char *name;
	char member_name[256];
	int len;

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return;
	name = expr_to_var(expr);
	if (!name)
		return;

	member_name[sizeof(member_name) - 1] = '\0';
	strcpy(member_name, "$$");

	len = strlen(name);
	FOR_EACH_PTR(returned_member_callbacks, cb) {
		my_slist = get_all_states_slist(cb->owner, slist);
		FOR_EACH_PTR(my_slist, sm) {
			if (strncmp(sm->name, name, len) != 0)
				continue;
			if (strncmp(sm->name + len, "->", 2) != 0)
				continue;
			strncpy(member_name + 2, sm->name + len, sizeof(member_name) - 2);
			cb->callback(return_id, return_ranges, member_name, sm->state);
		} END_FOR_EACH_PTR(sm);
		free_slist(&my_slist);
	} END_FOR_EACH_PTR(cb);

	free_string(name);
}

static void match_end_func_info(struct symbol *sym)
{
	if (__path_is_null())
		return;
	call_return_state_hooks(NULL);
}

void open_smatch_db(void)
{
#ifdef SQLITE_OPEN_READONLY
	int rc;

	if (option_no_db)
		return;

	rc = sqlite3_open_v2("smatch_db.sqlite", &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		option_no_db = 1;
		return;
	}
	return;
#else
	option_no_db = 1;
	return;
#endif
}

void register_definition_db_callbacks(int id)
{
	add_hook(&match_function_def, FUNC_DEF_HOOK);

	if (option_info) {
		add_hook(&match_call_info, FUNCTION_CALL_HOOK);
		add_hook(&match_call_hack, FUNCTION_CALL_HOOK);
		add_hook(&match_function_assign, ASSIGNMENT_HOOK);
		add_hook(&match_function_assign, GLOBAL_ASSIGNMENT_HOOK);
		add_hook(&global_variable, BASE_HOOK);
		add_hook(&global_variable, DECLARATION_HOOK);
		add_returned_state_callback(match_return_info);
		add_returned_state_callback(print_returned_struct_members);
		add_hook(&call_return_state_hooks, RETURN_HOOK);
		add_hook(&match_end_func_info, END_FUNC_HOOK);
	}

	if (option_no_db)
		return;

	add_hook(&match_data_from_db, FUNC_DEF_HOOK);
	add_hook(&match_call_implies, FUNCTION_CALL_HOOK);
}
