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
 * add_conditional_hook()     - For when the return value implies something.
 *                              For example a return value of 1 might mean
 *                              a lock is held and 0 means it is not held.
 */

#define _GNU_SOURCE
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

struct fcall_back {
	int type;
	struct data_range *range;
	func_hook *call_back;
	void *info;
};

ALLOCATOR(fcall_back, "call backs");
DECLARE_PTR_LIST(call_back_list, struct fcall_back);

static struct hsearch_data func_hash;

#define REGULAR_CALL     0
#define CONDITIONAL_CALL 1
#define ASSIGN_CALL      2
#define RANGED_CALL      3

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

static struct call_back_list *get_call_backs(const char *look_for)
{
	ENTRY e, *ep;

	e.key = (char *)look_for;
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep)
		return NULL;
	return (struct call_back_list *)ep->data;
}

static void add_cb_hook(const char *look_for, struct fcall_back *cb)
{
	ENTRY e, *ep;
	char *old_key = NULL;

	e.key = alloc_string(look_for);
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep) {
		struct call_back_list *list = NULL;
		
		add_ptr_list(&list, cb);
		e.data = list;
	} else {
		old_key = e.key;
		e.key = ep->key;
		add_ptr_list((struct call_back_list **)&ep->data, cb);
		e.data = ep->data;
	}
	if (!hsearch_r(e, ENTER, &ep, &func_hash)) {
		printf("Error hash table too small in smatch_function_hooks.c\n");
		exit(1);
	}
	free_string(old_key);
}

void add_function_hook(const char *look_for, func_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(REGULAR_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

void add_conditional_hook(const char *look_for, func_hook *call_back,
			  void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(CONDITIONAL_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(ASSIGN_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

void return_implies_state(const char *look_for, long long start, long long end,
			 implication_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(RANGED_CALL, (func_hook *)call_back, info);
	cb->range = alloc_range_perm(start, end);
	add_cb_hook(look_for, cb); 
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
	call_backs = get_call_backs(expr->fn->symbol->ident->name);
	if (!call_backs)
		return;
	call_call_backs(call_backs, REGULAR_CALL, expr->fn->symbol->ident->name,
			expr);
}

static void assign_condition_funcs(const char *fn, struct expression *expr,
				 struct call_back_list *call_backs)
{
	struct fcall_back *tmp;
	struct sm_state *sm;
	int conditional = 0;
	char *var_name;
	struct symbol *sym;
	struct smatch_state *zero_state, *non_zero_state;

	var_name = get_variable_from_expr(expr->left, &sym);
	if (!var_name || !sym)
		goto free;

	__fake_conditions = 1;
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != CONDITIONAL_CALL)
			continue;

		conditional = 1;
		(tmp->call_back)(fn, expr->right, tmp->info);
	} END_FOR_EACH_PTR(tmp);
	if (conditional) {
		zero_state = alloc_extra_state(0);
		non_zero_state = add_filter(extra_undefined(), 0);
		set_true_false_states(SMATCH_EXTRA, var_name, sym, non_zero_state, zero_state);
	}
  	__fake_conditions = 0;

	if (!conditional)
		goto free;

	merge_slist(&__fake_cond_true, __fake_cond_false);

	FOR_EACH_PTR(__fake_cond_true, sm) {
		__set_state(sm);
	} END_FOR_EACH_PTR(sm);
	free_slist(&__fake_cond_true);
	free_slist(&__fake_cond_false);
free:
	free_string(var_name);
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
	struct state_list *final_states = NULL;
	struct range_list *handled_ranges = NULL;
	struct call_back_list *same_range_call_backs = NULL;

	var_name = get_variable_from_expr(expr->left, &sym);
	if (!var_name || !sym)
		goto free;

	__fake_cur = 1;
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (in_list_exact(handled_ranges, tmp->range))
			continue;
		tack_on(&handled_ranges, tmp->range);

		same_range_call_backs = get_same_ranged_call_backs(call_backs, tmp->range);
		call_ranged_call_backs(same_range_call_backs, fn, expr->right, expr);
 		__free_ptr_list((struct ptr_list **)&same_range_call_backs);

		extra_state = alloc_extra_state_range(tmp->range->min, tmp->range->max);
		set_state(SMATCH_EXTRA, var_name, sym, extra_state);

		merge_slist(&final_states, __fake_cur_slist);
		free_slist(&__fake_cur_slist);
	} END_FOR_EACH_PTR(tmp);
  	__fake_cur = 0;

	FOR_EACH_PTR(final_states, sm) {
		__set_state(sm);
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
	struct sm_state *sm;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;
	fn = expr->fn->symbol->ident->name;
	call_backs = get_call_backs(expr->fn->symbol->ident->name);
	if (!call_backs)
		return;
	value_range = alloc_range(value, value);

	__fake_cur = 1;
	/* set true states */
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (!true_comparison_range_lr(comparison, tmp->range, value_range, left))
			continue;
		((implication_hook *)(tmp->call_back))(fn, expr, NULL, tmp->info);
		merge_slist(&true_states, __fake_cur_slist);
		free_slist(&__fake_cur_slist);
	} END_FOR_EACH_PTR(tmp);

	/* set false states */
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != RANGED_CALL)
			continue;
		if (!false_comparison_range_lr(comparison, tmp->range, value_range, left))
			continue;
		((implication_hook *)(tmp->call_back))(fn, expr, NULL, tmp->info);
		merge_slist(&false_states, __fake_cur_slist);
		free_slist(&__fake_cur_slist);
	} END_FOR_EACH_PTR(tmp);
	__fake_cur = 0;

	FOR_EACH_PTR(true_states, sm) {
		__set_true_false_sm(sm, NULL);
	} END_FOR_EACH_PTR(sm);
	FOR_EACH_PTR(false_states, sm) {
		__set_true_false_sm(NULL, sm);
	} END_FOR_EACH_PTR(sm);

	if (true_states && !false_states)
		sm_msg("warning:  unhandled false condition.");
	if (!true_states && false_states)
		sm_msg("warning:  unhandled true condition.");
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
	call_backs = get_call_backs(fn);
	if (!call_backs)
		return;
	call_call_backs(call_backs, ASSIGN_CALL, fn, expr);
	assign_condition_funcs(fn, expr, call_backs);
	assign_ranged_funcs(fn, expr, call_backs);
}

static void match_conditional_call(struct expression *expr)
{
	struct call_back_list *call_backs;
	struct fcall_back *tmp;
	struct sm_state *sm;
	const char *fn;

	expr = strip_expr(expr);
	if (expr->type != EXPR_CALL)
		return;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	fn = expr->fn->symbol->ident->name;
	call_backs = get_call_backs(fn);
	if (!call_backs)
		return;
	__fake_conditions = 1;
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != CONDITIONAL_CALL)
			continue;

		(tmp->call_back)(fn, expr, tmp->info);

		FOR_EACH_PTR(__fake_cond_true, sm) {
			__set_true_false_sm(sm, NULL);
		} END_FOR_EACH_PTR(sm);
		free_slist(&__fake_cond_true);

		FOR_EACH_PTR(__fake_cond_false, sm) {
			__set_true_false_sm(NULL, sm);
		} END_FOR_EACH_PTR(sm);
		free_slist(&__fake_cond_false);

	} END_FOR_EACH_PTR(tmp);
	__fake_conditions = 0;
}

void create_function_hash(void)
{
	hcreate_r(10000, &func_hash);  // Apparently 1000 is too few...
}

void register_function_hooks(int id)
{
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_conditional_call, CONDITION_HOOK);
}
