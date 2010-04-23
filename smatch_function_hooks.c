/*
 * sparse/smatch_function_hooks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * There are three types of function hooks:
 * add_function_hook()        - For any time a function is called.
 * add_function_assign_hook() - foo = the_function().
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
	func_hook *call_back;
	void *info;
};

ALLOCATOR(fcall_back, "call backs");
DECLARE_PTR_LIST(call_back_list, struct fcall_back);

DEFINE_FUNCTION_HASHTABLE_STATIC(callback, struct fcall_back, struct call_back_list);
static struct hashtable *func_hash;

#define REGULAR_CALL     0
#define ASSIGN_CALL      1
#define RANGED_CALL      2

static struct fcall_back *alloc_fcall_back(int type, func_hook *call_back,
					   void *info)
{
	struct fcall_back *cb;

	cb = __alloc_fcall_back(0);
	cb->type = type;
	cb->call_back = call_back;
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

void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(RANGED_CALL, (func_hook *)call_back, info);
	cb->range = alloc_range_perm(start, end);
	add_callback(func_hash, look_for, cb); 
}

static void call_call_backs(struct call_back_list *list, int type,
			    const char *fn, struct expression *expr)
{
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->type == type)
			(tmp->call_back)(fn, expr, tmp->info);
	} END_FOR_EACH_PTR(tmp);
}

static void call_ranged_call_backs(struct call_back_list *list,
				const char *fn, struct expression *call_expr,
				struct expression *assign_expr)
{
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		((implication_hook *)(tmp->call_back))(fn, call_expr, assign_expr, tmp->info);
	} END_FOR_EACH_PTR(tmp);
}

static void match_function_call(struct expression *expr)
{
	struct call_back_list *call_backs;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;
	call_backs = search_callback(func_hash, (char *)expr->fn->symbol->ident->name);
	if (!call_backs)
		return;
	call_call_backs(call_backs, REGULAR_CALL, expr->fn->symbol->ident->name,
			expr);
}

static struct call_back_list *get_same_ranged_call_backs(struct call_back_list *list,
						struct data_range *drange)
{
	struct call_back_list *ret = NULL;
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (tmp->range->min == drange->min && tmp->range->max == drange->max)
			add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static void assign_ranged_funcs(const char *fn, struct expression *expr,
				 struct call_back_list *call_backs)
{
	struct fcall_back *tmp;
	struct sm_state *sm;
	char *var_name;
	struct symbol *sym;
	struct smatch_state *extra_state;
	struct state_list *tmp_slist;
	struct state_list *final_states = NULL;
	struct range_list *handled_ranges = NULL;
	struct call_back_list *same_range_call_backs = NULL;

	var_name = get_variable_from_expr(expr->left, &sym);
	if (!var_name || !sym)
		goto free;

	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (in_list_exact(handled_ranges, tmp->range))
			continue;
		__push_fake_cur_slist();
		tack_on(&handled_ranges, tmp->range);

		same_range_call_backs = get_same_ranged_call_backs(call_backs, tmp->range);
		call_ranged_call_backs(same_range_call_backs, fn, expr->right, expr);
 		__free_ptr_list((struct ptr_list **)&same_range_call_backs);

		extra_state = alloc_extra_state_range(tmp->range->min, tmp->range->max);
		set_extra_mod(var_name, sym, extra_state);

		tmp_slist = __pop_fake_cur_slist();
		merge_slist(&final_states, tmp_slist);
		free_slist(&tmp_slist);
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&final_states);
free:
	free_string(var_name);
}

void function_comparison(int comparison, struct expression *expr, long long value, int left)
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
		return;
	fn = expr->fn->symbol->ident->name;
	call_backs = search_callback(func_hash, (char *)expr->fn->symbol->ident->name);
	if (!call_backs)
		return;
	value_range = alloc_range(value, value);

	/* set true states */
	__push_fake_cur_slist();
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (!true_comparison_range_lr(comparison, tmp->range, value_range, left))
			continue;
		((implication_hook *)(tmp->call_back))(fn, expr, NULL, tmp->info);
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
		((implication_hook *)(tmp->call_back))(fn, expr, NULL, tmp->info);
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
}

static void match_assign_call(struct expression *expr)
{
	struct call_back_list *call_backs;
	const char *fn;
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->fn->type != EXPR_SYMBOL || !right->fn->symbol)
		return;
	fn = right->fn->symbol->ident->name;
	call_backs = search_callback(func_hash, (char *)fn);
	if (!call_backs)
		return;
	call_call_backs(call_backs, ASSIGN_CALL, fn, expr);
	assign_ranged_funcs(fn, expr, call_backs);
}

void create_function_hook_hash(void)
{
	func_hash = create_function_hashtable(5000);
}

void register_function_hooks(int id)
{
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);
}
