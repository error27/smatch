/*
 * sparse/smatch_function_hooks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * There are several types of function hooks:
 * add_function_hook()        - For any time a function is called.
 * add_function_assign_hook() - foo = the_function().
 * add_implied_return_hook()  - Calculates the implied return value.
 * add_macro_assign_hook()    - foo = the_macro().
 * return_implies_state()     - For when a return value of 1 implies locked
 *                              and 0 implies unlocked. etc. etc.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include "smatch_function_hashtable.h"

struct fcall_back {
	int type;
	struct data_range *range;
	union {
		func_hook *call_back;
		implication_hook *ranged;
		implied_return_hook *implied_return;
	} u;
	void *info;
};

ALLOCATOR(fcall_back, "call backs");
DECLARE_PTR_LIST(call_back_list, struct fcall_back);

DEFINE_FUNCTION_HASHTABLE_STATIC(callback, struct fcall_back, struct call_back_list);
static struct hashtable *func_hash;

#define REGULAR_CALL       0
#define RANGED_CALL        1
#define ASSIGN_CALL        2
#define IMPLIED_RETURN     3
#define MACRO_ASSIGN       4
#define MACRO_ASSIGN_EXTRA 5

struct return_implies_callback {
	int type;
	return_implies_hook *callback;
};
ALLOCATOR(return_implies_callback, "return_implies callbacks");
DECLARE_PTR_LIST(db_implies_list, struct return_implies_callback);
static struct db_implies_list *db_return_states_list;

typedef void (void_fn)(void);
DECLARE_PTR_LIST(void_fn_list, void_fn *);
static struct void_fn_list *return_states_before;
static struct void_fn_list *return_states_after;

static struct fcall_back *alloc_fcall_back(int type, void *call_back,
					   void *info)
{
	struct fcall_back *cb;

	cb = __alloc_fcall_back(0);
	cb->type = type;
	cb->u.call_back = call_back;
	cb->info = info;
	return cb;
}

void add_function_hook(const char *look_for, func_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(REGULAR_CALL, call_back, info);
	add_callback(func_hash, look_for, cb);
}

void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(ASSIGN_CALL, call_back, info);
	add_callback(func_hash, look_for, cb);
}

void add_implied_return_hook(const char *look_for,
			     implied_return_hook *call_back,
			     void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(IMPLIED_RETURN, call_back, info);
	add_callback(func_hash, look_for, cb);
}

void add_macro_assign_hook(const char *look_for, func_hook *call_back,
			void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(MACRO_ASSIGN, call_back, info);
	add_callback(func_hash, look_for, cb);
}

void add_macro_assign_hook_extra(const char *look_for, func_hook *call_back,
			void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(MACRO_ASSIGN_EXTRA, call_back, info);
	add_callback(func_hash, look_for, cb);
}

void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(RANGED_CALL, call_back, info);
	cb->range = alloc_range_perm(ll_to_sval(start), ll_to_sval(end));
	add_callback(func_hash, look_for, cb);
}

void add_db_return_states_callback(int type, return_implies_hook *callback)
{
	struct return_implies_callback *cb = __alloc_return_implies_callback(0);

	cb->type = type;
	cb->callback = callback;
	add_ptr_list(&db_return_states_list, cb);
}

void add_db_return_states_before(void_fn *fn)
{
	void_fn **p = malloc(sizeof(void_fn *));
	*p = fn;
	add_ptr_list(&return_states_before, p);
}

void add_db_return_states_after(void_fn *fn)
{
	void_fn **p = malloc(sizeof(void_fn *));
	*p = fn;
	add_ptr_list(&return_states_after, p);
}

static void call_return_states_before_hooks(void)
{
	void_fn **fn;

	FOR_EACH_PTR(return_states_before, fn) {
		(*fn)();
	} END_FOR_EACH_PTR(fn);
}

static void call_return_states_after_hooks(void)
{
	void_fn **fn;

	FOR_EACH_PTR(return_states_after, fn) {
		(*fn)();
	} END_FOR_EACH_PTR(fn);
}

static int call_call_backs(struct call_back_list *list, int type,
			    const char *fn, struct expression *expr)
{
	struct fcall_back *tmp;
	int handled = 0;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->type == type) {
			(tmp->u.call_back)(fn, expr, tmp->info);
			handled = 1;
		}
	} END_FOR_EACH_PTR(tmp);

	return handled;
}

static void call_ranged_call_backs(struct call_back_list *list,
				const char *fn, struct expression *call_expr,
				struct expression *assign_expr)
{
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		(tmp->u.ranged)(fn, call_expr, assign_expr, tmp->info);
	} END_FOR_EACH_PTR(tmp);
}

static struct call_back_list *get_same_ranged_call_backs(struct call_back_list *list,
						struct data_range *drange)
{
	struct call_back_list *ret = NULL;
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (ranges_equiv(tmp->range, drange))
			add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static int in_list_exact_sval(struct range_list *list, struct data_range *drange)
{
	struct data_range *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (ranges_equiv(tmp, drange))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static int assign_ranged_funcs(const char *fn, struct expression *expr,
				 struct call_back_list *call_backs)
{
	struct fcall_back *tmp;
	struct sm_state *sm;
	char *var_name;
	struct symbol *sym;
	struct smatch_state *estate;
	struct state_list *tmp_slist;
	struct state_list *final_states = NULL;
	struct range_list *handled_ranges = NULL;
	struct call_back_list *same_range_call_backs = NULL;
	int handled = 0;

	if (!call_backs)
		return 0;

	var_name = get_variable_from_expr(expr->left, &sym);
	if (!var_name || !sym)
		goto free;

	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;

		if (in_list_exact_sval(handled_ranges, tmp->range))
			continue;
		__push_fake_cur_slist();
		tack_on(&handled_ranges, tmp->range);

		same_range_call_backs = get_same_ranged_call_backs(call_backs, tmp->range);
		call_ranged_call_backs(same_range_call_backs, fn, expr->right, expr);
		__free_ptr_list((struct ptr_list **)&same_range_call_backs);

		estate = alloc_estate_range(tmp->range->min, tmp->range->max);
		set_extra_mod(var_name, sym, estate);

		tmp_slist = __pop_fake_cur_slist();
		merge_slist(&final_states, tmp_slist);
		free_slist(&tmp_slist);
		handled = 1;
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&final_states);
free:
	free_string(var_name);
	return handled;
}

static int call_implies_callbacks(int comparison, struct expression *expr, sval_t sval, int left)
{
	struct call_back_list *call_backs;
	struct fcall_back *tmp;
	const char *fn;
	struct data_range *value_range;
	struct state_list *true_states = NULL;
	struct state_list *false_states = NULL;
	struct state_list *tmp_slist;
	struct sm_state *sm;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return 0;
	fn = expr->fn->symbol->ident->name;
	call_backs = search_callback(func_hash, (char *)expr->fn->symbol->ident->name);
	if (!call_backs)
		return 0;
	value_range = alloc_range(sval, sval);

	/* set true states */
	__push_fake_cur_slist();
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (!true_comparison_range_lr(comparison, tmp->range, value_range, left))
			continue;
		(tmp->u.ranged)(fn, expr, NULL, tmp->info);
	} END_FOR_EACH_PTR(tmp);
	tmp_slist = __pop_fake_cur_slist();
	merge_slist(&true_states, tmp_slist);
	free_slist(&tmp_slist);

	/* set false states */
	__push_fake_cur_slist();
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (!false_comparison_range_lr(comparison, tmp->range, value_range, left))
			continue;
		(tmp->u.ranged)(fn, expr, NULL, tmp->info);
	} END_FOR_EACH_PTR(tmp);
	tmp_slist = __pop_fake_cur_slist();
	merge_slist(&false_states, tmp_slist);
	free_slist(&tmp_slist);

	FOR_EACH_PTR(true_states, sm) {
		__set_true_false_sm(sm, NULL);
	} END_FOR_EACH_PTR(sm);
	FOR_EACH_PTR(false_states, sm) {
		__set_true_false_sm(NULL, sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&true_states);
	free_slist(&false_states);
	return 1;
}

struct db_callback_info {
	int true_side;
	int comparison;
	struct expression *expr;
	struct range_list *rl;
	int left;
	struct state_list *slist;
	struct db_implies_list *callbacks;
};
static struct db_callback_info db_info;
static int db_compare_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct range_list *ret_range;
	int type, param;
	char *key, *value;
	struct return_implies_callback *tmp;

	if (argc != 5)
		return 0;

	parse_value_ranges_type(get_type(db_info.expr), argv[0], &ret_range);
	type = atoi(argv[1]);
	param = atoi(argv[2]);
	key = argv[3];
	value = argv[4];

	if (db_info.true_side) {
		if (!possibly_true_range_lists_lr(db_info.comparison,
						  ret_range, db_info.rl,
						  db_info.left))
			return 0;
	} else {
		if (!possibly_false_range_lists_lr(db_info.comparison,
						  ret_range, db_info.rl,
						  db_info.left))
			return 0;
	}

	FOR_EACH_PTR(db_info.callbacks, tmp) {
		if (tmp->type == type)
			tmp->callback(db_info.expr, param, key, value);
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

void compare_db_return_states_callbacks(int comparison, struct expression *expr, sval_t sval, int left)
{
	struct symbol *sym;
        static char sql_filter[1024];
	struct state_list *true_states;
	struct state_list *false_states;
	struct sm_state *sm;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	sym = expr->fn->symbol;
	if (!sym)
		return;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024,
			 "file = '%s' and function = '%s' and static = '1';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024,
			 "function = '%s' and static = '0';", sym->ident->name);
	}

	db_info.comparison = comparison;
	db_info.expr = expr;
	db_info.rl = alloc_rl(sval, sval);
	db_info.left = left;
	db_info.callbacks = db_return_states_list;

	call_return_states_before_hooks();

	db_info.true_side = 1;
	__push_fake_cur_slist();
	run_sql(db_compare_callback,
		"select return, type, parameter, key, value from return_states where %s",
		sql_filter);
	true_states = __pop_fake_cur_slist();

	db_info.true_side = 0;
	__push_fake_cur_slist();
	run_sql(db_compare_callback,
		"select return, type, parameter, key, value from return_states where %s",
		sql_filter);
	false_states = __pop_fake_cur_slist();

	FOR_EACH_PTR(true_states, sm) {
		__set_true_false_sm(sm, NULL);
	} END_FOR_EACH_PTR(sm);
	FOR_EACH_PTR(false_states, sm) {
		__set_true_false_sm(NULL, sm);
	} END_FOR_EACH_PTR(sm);

	call_return_states_after_hooks();

	free_slist(&true_states);
	free_slist(&false_states);
}



void function_comparison(int comparison, struct expression *expr, sval_t sval, int left)
{
	if (call_implies_callbacks(comparison, expr, sval, left))
		return;
	compare_db_return_states_callbacks(comparison, expr, sval, left);
}

static int prev_return_id;
static int db_assign_return_states_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct range_list *ret_range;
	int type, param;
	char *key, *value;
	struct return_implies_callback *tmp;
	struct state_list *slist;
	int return_id;

	if (argc != 6)
		return 0;

	return_id = atoi(argv[0]);
	parse_value_ranges_type(get_type(db_info.expr->right), argv[1], &ret_range);
	if (!ret_range)
		ret_range = alloc_whole_rl(cur_func_return_type());
	type = atoi(argv[2]);
	param = atoi(argv[3]);
	key = argv[4];
	value = argv[5];

	if (prev_return_id != -1 && return_id != prev_return_id) {
		slist = __pop_fake_cur_slist();
		merge_slist(&db_info.slist, slist);
		__push_fake_cur_slist();
	}
	prev_return_id = return_id;

	FOR_EACH_PTR(db_return_states_list, tmp) {
		if (tmp->type == type)
			tmp->callback(db_info.expr, param, key, value);
	} END_FOR_EACH_PTR(tmp);
	ret_range = cast_rl(get_type(db_info.expr->left), ret_range);
	set_extra_expr_mod(db_info.expr->left, alloc_estate_range_list(ret_range));

	return 0;
}

static int db_return_states_assign(struct expression *expr)
{
	struct expression *right;
	struct symbol *sym;
	struct sm_state *sm;
	struct state_list *slist;
        static char sql_filter[1024];
	int handled = 0;

	right = strip_expr(expr->right);
	if (right->fn->type != EXPR_SYMBOL || !right->fn->symbol)
		return 0;

	sym = right->fn->symbol;
	if (!sym)
		return 0;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024,
			 "file = '%s' and function = '%s' and static = '1';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024,
			 "function = '%s' and static = '0';", sym->ident->name);
	}

	prev_return_id = -1;
	db_info.expr = expr;
	db_info.slist = NULL;

	call_return_states_before_hooks();

	__push_fake_cur_slist();
	run_sql(db_assign_return_states_callback,
		"select return_id, return, type, parameter, key, value from return_states where %s",
		sql_filter);
	slist = __pop_fake_cur_slist();
	merge_slist(&db_info.slist, slist);

	FOR_EACH_PTR(db_info.slist, sm) {
		__set_sm(sm);
		handled = 1;
	} END_FOR_EACH_PTR(sm);

	call_return_states_after_hooks();

	return handled;
}

static int handle_implied_return(struct expression *expr)
{
	struct range_list *rl;

	if (!get_implied_return(expr->right, &rl))
		return 0;
	rl = cast_rl(get_type(expr->left), rl);
	set_extra_expr_mod(expr->left, alloc_estate_range_list(rl));
	return 1;
}

static void match_assign_call(struct expression *expr)
{
	struct call_back_list *call_backs;
	const char *fn;
	struct expression *right;
	int handled = 0;

	right = strip_expr(expr->right);
	if (right->fn->type != EXPR_SYMBOL || !right->fn->symbol) {
		set_extra_expr_mod(expr->left, extra_undefined(get_type(expr->left)));
		return;
	}
	fn = right->fn->symbol->ident->name;

	/*
	 * some of these conflict (they try to set smatch extra twice), so we
	 * call them in order from least important to most important.
	 */

	call_backs = search_callback(func_hash, (char *)fn);
	call_call_backs(call_backs, ASSIGN_CALL, fn, expr);

	handled |= db_return_states_assign(expr);
	handled |= assign_ranged_funcs(fn, expr, call_backs);
	handled |= handle_implied_return(expr);

	if (!handled) {
		struct range_list *rl;

		if (!get_implied_range_list(expr->right, &rl))
			rl = alloc_whole_rl(get_type(expr->right));
		rl = cast_rl(get_type(expr->left), rl);
		set_extra_expr_mod(expr->left, alloc_estate_range_list(rl));
	}
}

static int db_return_states_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct range_list *ret_range;
	int type, param;
	char *key, *value;
	struct return_implies_callback *tmp;
	struct state_list *slist;
	int return_id;

	if (argc != 6)
		return 0;

	return_id = atoi(argv[0]);
	parse_value_ranges_type(get_type(db_info.expr), argv[1], &ret_range);
	type = atoi(argv[2]);
	param = atoi(argv[3]);
	key = argv[4];
	value = argv[5];

	if (prev_return_id != -1 && return_id != prev_return_id) {
		slist = __pop_fake_cur_slist();
		merge_slist(&db_info.slist, slist);
		__push_fake_cur_slist();
		__unnullify_path();
	}
	prev_return_id = return_id;

	FOR_EACH_PTR(db_return_states_list, tmp) {
		if (tmp->type == type)
			tmp->callback(db_info.expr, param, key, value);
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static void db_return_states(struct expression *expr)
{
	struct symbol *sym;
	struct sm_state *sm;
	struct state_list *slist;
        static char sql_filter[1024];

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	sym = expr->fn->symbol;
	if (!sym)
		return;

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024,
			 "file = '%s' and function = '%s' and static = '1';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024,
			 "function = '%s' and static = '0';", sym->ident->name);
	}

	prev_return_id = -1;
	db_info.expr = expr;
	db_info.slist = NULL;

	call_return_states_before_hooks();

	__push_fake_cur_slist();
	__unnullify_path();
	run_sql(db_return_states_callback,
		"select return_id, return, type, parameter, key, value from return_states where %s",
		sql_filter);
	slist = __pop_fake_cur_slist();
	merge_slist(&db_info.slist, slist);

	FOR_EACH_PTR(db_info.slist, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	call_return_states_after_hooks();
}

static int is_assigned_call(struct expression *expr)
{
	struct expression *tmp;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (tmp->type == EXPR_ASSIGNMENT && strip_expr(tmp->right) == expr)
			return 1;
		if (tmp->pos.line < expr->pos.line)
			return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static void db_return_states_call(struct expression *expr)
{
	if (is_assigned_call(expr))
		return;
	db_return_states(expr);
}

static void match_function_call(struct expression *expr)
{
	struct call_back_list *call_backs;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;
	call_backs = search_callback(func_hash, (char *)expr->fn->symbol->ident->name);
	if (call_backs)
		call_call_backs(call_backs, REGULAR_CALL,
				expr->fn->symbol->ident->name, expr);
	db_return_states_call(expr);
}

static void match_macro_assign(struct expression *expr)
{
	struct call_back_list *call_backs;
	const char *macro;
	struct expression *right;

	right = strip_expr(expr->right);
	macro = get_macro_name(right->pos);
	call_backs = search_callback(func_hash, (char *)macro);
	if (!call_backs)
		return;
	call_call_backs(call_backs, MACRO_ASSIGN, macro, expr);
	call_call_backs(call_backs, MACRO_ASSIGN_EXTRA, macro, expr);
}

int get_implied_return(struct expression *expr, struct range_list **rl)
{
	struct call_back_list *call_backs;
	struct fcall_back *tmp;
	int handled = 0;
	char *fn;

	*rl = NULL;

	expr = strip_expr(expr);
	fn = get_variable_from_expr(expr->fn, NULL);
	if (!fn)
		goto out;

	call_backs = search_callback(func_hash, fn);

	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type == IMPLIED_RETURN) {
			(tmp->u.implied_return)(expr, tmp->info, rl);
			handled = 1;
		}
	} END_FOR_EACH_PTR(tmp);

out:
	free_string(fn);
	return handled;
}

void create_function_hook_hash(void)
{
	func_hash = create_function_hashtable(5000);
}

void register_function_hooks(int id)
{
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_macro_assign, MACRO_ASSIGNMENT_HOOK);
}
