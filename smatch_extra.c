/*
 * sparse/smatch_extra.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#define __USE_ISOC99 
#include <limits.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

struct data_range whole_range = {
	.min = LLONG_MIN,
	.max = LLONG_MAX,	
};

static struct smatch_state *alloc_extra_state_no_name(int val)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	if (val == UNDEFINED)
		state->data = (void *)alloc_dinfo_range(whole_range.min, whole_range.max);
	else
		state->data = (void *)alloc_dinfo_range(val, val);
	return state;
}

struct smatch_state *alloc_extra_state(int val)
{
	struct smatch_state *state;

	state = alloc_extra_state_no_name(val);
	state->name = show_ranges(((struct data_info *)state->data)->value_ranges);
	return state;
}


/* We do this because ->value_ranges is a list */
struct smatch_state *extra_undefined()
{
	static struct data_info *dinfo;
	static struct smatch_state *ret;

	dinfo = alloc_dinfo_range(whole_range.min, whole_range.max);
	ret = __alloc_smatch_state(0);
	ret->name = "unknown";
	ret->data = dinfo;
	return ret;
}

struct smatch_state *filter_ranges(struct smatch_state *orig,
				 long long filter_min, long long filter_max)
{
	struct smatch_state *ret;
	struct data_info *orig_info;
	struct data_info *ret_info;

	if (!orig)
		orig = extra_undefined();
	orig_info = (struct data_info *)orig->data;
	ret = alloc_extra_state_no_name(UNDEFINED);
	ret_info = (struct data_info *)ret->data;
	ret_info->value_ranges = remove_range(orig_info->value_ranges, filter_min, filter_max);
	ret->name = show_ranges(ret_info->value_ranges);
	return ret;
}

struct smatch_state *add_filter(struct smatch_state *orig, long long num)
{
	return filter_ranges(orig, num, num);
}

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	struct data_info *info1 = (struct data_info *)s1->data;
	struct data_info *info2 = (struct data_info *)s2->data;
	struct data_info *ret_info;
	struct smatch_state *tmp;

	tmp = alloc_extra_state_no_name(UNDEFINED);
	tmp->name = "extra_merged";
	ret_info = (struct data_info *)tmp->data;
	ret_info->merged = 1;
	ret_info->value_ranges = range_list_union(info1->value_ranges, info2->value_ranges);
	return tmp;
}

struct sm_state *__extra_merge(struct sm_state *one, struct state_list *slist1,
			       struct sm_state *two, struct state_list *slist2)
{
	struct data_info *info1;
	struct data_info *info2;

	if (!one->state->data || !two->state->data) {
		smatch_msg("internal error in smatch extra '%s = %s or %s'",
			   one->name, show_state(one->state),
			   show_state(two->state));
		return alloc_state(one->name, one->owner, one->sym,
				   extra_undefined());
	}

	info1 = (struct data_info *)one->state->data;
	info2 = (struct data_info *)two->state->data;

	if (!info1->merged)
		free_stack(&one->my_pools);
	if (!info2->merged)
		free_stack(&two->my_pools);

	if (one == two && !one->my_pools) {
		add_pool(&one->my_pools, slist1);
		add_pool(&one->my_pools, slist2);
	} else {
		if (!one->my_pools)
			add_pool(&one->my_pools, slist1);
		if (!two->my_pools)
			add_pool(&two->my_pools, slist2);
	}

	add_pool(&one->all_pools, slist1);
	add_pool(&two->all_pools, slist2);
	return merge_sm_states(one, two);
}

struct sm_state *__extra_and_merge(struct sm_state *sm,
				     struct state_list_stack *stack)
{
	struct state_list *slist;
	struct sm_state *ret = NULL;
	struct sm_state *tmp;
	int i = 0;

	FOR_EACH_PTR(stack, slist) {
		if (!i++) {
			ret = get_sm_state_slist(slist, sm->name, sm->owner,
						 sm->sym);
		} else {
			tmp = get_sm_state_slist(slist, sm->name, sm->owner,
						 sm->sym);
			ret = merge_sm_states(ret, tmp);
		}
	} END_FOR_EACH_PTR(slist);
	if (!ret) {
		smatch_msg("Internal error in __extra_and_merge");
		return NULL;
	}
	ret->my_pools = stack;
	ret->all_pools = clone_stack(stack);
	return ret;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return extra_undefined();
}

static void match_function_call(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	int i = 0;

	FOR_EACH_PTR(expr->args, tmp) {
		if (tmp->op == '&') {
			name = get_variable_from_expr(tmp->unop, &sym);
			if (name) {
				set_state(name, my_id, sym, extra_undefined());
			}
			free_string(name);
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void match_assign(struct expression *expr)
{
	struct expression *left;
	struct symbol *sym;
	char *name;
	
	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name)
		return;
	set_state(name, my_id, sym, alloc_extra_state(get_value(expr->right)));
	free_string(name);
}

static void undef_expr(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	
	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		return;
	if (!get_state(name, my_id, sym)) {
		free_string(name);
		return;
	}
	set_state(name, my_id, sym, extra_undefined());
	free_string(name);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if (sym->ident) {
		name = sym->ident->name;
		if (sym->initializer) {
			set_state(name, my_id, sym, alloc_extra_state(get_value(sym->initializer)));
		} else {
			set_state(name, my_id, sym, extra_undefined());
		}
	}
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		if (!arg->ident) {
			continue;
		}
		set_state(arg->ident->name, my_id, arg, extra_undefined());
	} END_FOR_EACH_PTR(arg);
}

static void match_unop(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	const char *tmp;
	

	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		return;

	tmp = show_special(expr->op);
	if ((!strcmp(tmp, "--")) || (!strcmp(tmp, "++")))
		set_state(name, my_id, sym, extra_undefined());
	free_string(name);
}

int get_implied_value(struct expression *expr)
{
	struct smatch_state *state;
	int val;
	struct symbol *sym;
	char *name;
	
	val = get_value(expr);
	if (val != UNDEFINED)
		return val;

	name = get_variable_from_expr(expr, &sym);
	if (!name)
		return UNDEFINED;
	state = get_state(name, my_id, sym);
	free_string(name);
	if (!state || !state->data)
		return UNDEFINED;
	return get_single_value_from_range((struct data_info *)state->data);
}

int true_comparison(int left, int comparison, int right)
{
	switch(comparison){
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left < right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left < right)
			return 1;
	case SPECIAL_EQUAL:
		if (left == right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left == right)
			return 1;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left > right)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left != right)
			return 1;
		return 0;
	default:
		smatch_msg("unhandled comparison %d\n", comparison);
		return UNDEFINED;
	}
	return 0;
}

int true_comparison_range(struct data_range *left, int comparison, struct data_range *right)
{
	switch(comparison){
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left->min < right->max)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left->min <= right->max)
			return 1;
		return 0;
	case SPECIAL_EQUAL:
		if (left->max < right->min)
			return 0;
		if (left->min > right->max)
			return 0;
		return 1;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left->max >= right->min)
			return 1;
		return 0;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left->max > right->min)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left->max < right->min)
			return 1;
		if (left->min > right->max)
			return 1;
		return 0;
	default:
		smatch_msg("unhandled comparison %d\n", comparison);
		return UNDEFINED;
	}
	return 0;
}

static int do_comparison(struct expression *expr)
{
	int left, right, ret;

	if ((left = get_implied_value(expr->left)) == UNDEFINED)
		return UNDEFINED;

	if ((right = get_implied_value(expr->right)) == UNDEFINED)
		return UNDEFINED;
	
	ret = true_comparison(left, expr->op, right);
	if (ret == 1) {
		SM_DEBUG("%d known condition: %d %s %d => true\n",
			get_lineno(), left, show_special(expr->op), right);
	} else if (ret == 0) {
		SM_DEBUG("%d known condition: %d %s %d => false\n",
			get_lineno(), left, show_special(expr->op), right);
	}
	return ret;
}

int last_stmt_val(struct statement *stmt)
{
	struct expression *expr;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return UNDEFINED;
	expr = stmt->expression;
	return get_value(expr);
}

static void match_comparison(struct expression *expr)
{
	int value;
	char *name = NULL;
	struct symbol *sym;
	struct smatch_state *eq_state;
	struct smatch_state *neq_state;

	if (expr->op != SPECIAL_EQUAL && expr->op != SPECIAL_NOTEQUAL)
		return;
	value = get_value(expr->left);
	if (value != UNDEFINED) {
		name = get_variable_from_expr(expr->right, &sym);
	} else {
		value = get_value(expr->right);
		name = get_variable_from_expr(expr->left, &sym);
	}
	if (value == UNDEFINED || !name || !sym)
		goto free;
	eq_state =  alloc_extra_state(value);
	neq_state = add_filter(extra_undefined(), value);
	if (expr->op == SPECIAL_EQUAL)
		set_true_false_states(name, my_id, sym, eq_state, neq_state);
	else
		set_true_false_states(name, my_id, sym, neq_state, eq_state);
free:
	free_string(name);
}

/* this is actually hooked from smatch_implied.c...  it's hacky, yes */
void __extra_match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	struct smatch_state *pre_state;
	struct smatch_state *true_state;
	struct smatch_state *false_state;

	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		name = get_variable_from_expr(expr, &sym);
		if (!name)
			return;
		pre_state = get_state(name, my_id, sym);
		true_state = add_filter(pre_state, 0);
		false_state = alloc_extra_state(0);
		set_true_false_states(name, my_id, sym, true_state, false_state);
		free_string(name);
		return;
	case EXPR_COMPARE:
		match_comparison(expr);
		return;
	}
}

static int variable_non_zero(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	int ret = UNDEFINED;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto exit;
	state = get_state(name, my_id, sym);
	if (!state || !state->data)
		goto exit;
	ret = true_comparison(get_single_value_from_range((struct data_info *)state->data),
			      SPECIAL_NOTEQUAL, 0);
exit:
	free_string(name);
	return ret;
}

int known_condition_true(struct expression *expr)
{
	int tmp;

	if (!expr)
		return 0;

	tmp = get_value(expr);
	if (tmp && tmp != UNDEFINED)
		return 1;
	
	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_false(expr->unop))
				return 1;
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

int known_condition_false(struct expression *expr)
{
	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	switch(expr->type) {
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_true(expr->unop))
				return 1;
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

int implied_condition_true(struct expression *expr)
{
	struct statement *stmt;
	int tmp;

	if (!expr)
		return 0;

	tmp = get_value(expr);
	if (tmp && tmp != UNDEFINED)
		return 1;
	
	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_COMPARE:
		if (do_comparison(expr) == 1)
			return 1;
		break;
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (implied_condition_false(expr->unop))
				return 1;
			break;
		}
		stmt = get_block_thing(expr);
		if (stmt && (last_stmt_val(stmt) == 1))
			return 1;
		break;
	default:
		if (variable_non_zero(expr) == 1)
			return 1;
		break;
	}
	return 0;
}

int implied_condition_false(struct expression *expr)
{
	struct statement *stmt;
	struct expression *tmp;

	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	switch(expr->type) {
	case EXPR_COMPARE:
		if (do_comparison(expr) == 0)
			return 1;
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (implied_condition_true(expr->unop))
				return 1;
			break;
		}
		stmt = get_block_thing(expr);
		if (stmt && (last_stmt_val(stmt) == 0))
			return 1;
		tmp = strip_expr(expr);
		if (tmp != expr)
			return implied_condition_false(tmp);
		break;
	default:
		if (variable_non_zero(expr) == 0)
			return 1;
		break;
	}
	return 0;
}

void register_smatch_extra(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&undef_expr, OP_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_unop, OP_HOOK);
	add_hook(&free_data_info_allocs, END_FUNC_HOOK);

#ifdef KERNEL
	/* I don't know how to test for the ATTRIB_NORET attribute. :( */
	add_function_hook("panic", &__match_nullify_path_hook, NULL);
	add_function_hook("do_exit", &__match_nullify_path_hook, NULL);
	add_function_hook("complete_and_exit", &__match_nullify_path_hook, NULL);
	add_function_hook("__module_put_and_exit", &__match_nullify_path_hook, NULL);
	add_function_hook("do_group_exit", &__match_nullify_path_hook, NULL);
#endif
}
