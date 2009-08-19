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

static struct symbol *cur_func;

struct data_range whole_range = {
	.min = LLONG_MIN,
	.max = LLONG_MAX,	
};

static struct smatch_state *alloc_extra_state_empty()
{
	struct smatch_state *state;
	struct data_info *dinfo;

	dinfo = __alloc_data_info(0);
	dinfo->type = DATA_RANGE;
	dinfo->value_ranges = NULL;
	state = __alloc_smatch_state(0);
	state->data = dinfo;
	return state;
}

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

/* We do this because ->value_ranges is a list */
struct smatch_state *extra_undefined()
{
	struct data_info *dinfo;
	static struct smatch_state *ret;
	static struct symbol *prev_func;

	if  (prev_func == cur_func)
		return ret;
	prev_func = cur_func;

	dinfo = alloc_dinfo_range(whole_range.min, whole_range.max);
	ret = __alloc_smatch_state(0);
	ret->name = "unknown";
	ret->data = dinfo;
	return ret;
}

struct smatch_state *alloc_extra_state(int val)
{
	struct smatch_state *state;

	if (val == UNDEFINED)
		return extra_undefined();
	state = alloc_extra_state_no_name(val);
	state->name = show_ranges(((struct data_info *)state->data)->value_ranges);
	return state;
}

struct smatch_state *alloc_extra_state_range(long long min, long long max)
{
	struct smatch_state *state;

	if (min == whole_range.min && max == whole_range.max)
		return extra_undefined();
	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range(min, max);
	state->name = show_ranges(((struct data_info *)state->data)->value_ranges);
	return state;
}

struct smatch_state *alloc_extra_state_range_list(struct range_list *rl)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range_list(rl);
	state->name = show_ranges(((struct data_info *)state->data)->value_ranges);
	return state;
}

struct smatch_state *filter_range(struct smatch_state *orig,
				 long long filter_min, long long filter_max)
{
	struct smatch_state *ret;
	struct data_info *orig_info;
	struct data_info *ret_info;

	if (!orig)
		orig = extra_undefined();
	orig_info = (struct data_info *)orig->data;
	ret = alloc_extra_state_empty();
	ret_info = (struct data_info *)ret->data;
	ret_info->value_ranges = remove_range(orig_info->value_ranges, filter_min, filter_max);
	ret->name = show_ranges(ret_info->value_ranges);
	return ret;
}

struct smatch_state *add_filter(struct smatch_state *orig, long long num)
{
	return filter_range(orig, num, num);
}

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	struct data_info *info1 = (struct data_info *)s1->data;
	struct data_info *info2 = (struct data_info *)s2->data;
	struct data_info *ret_info;
	struct smatch_state *tmp;
	struct range_list *value_ranges;

	value_ranges = range_list_union(info1->value_ranges, info2->value_ranges);
	tmp = alloc_extra_state_empty();
	ret_info = (struct data_info *)tmp->data;
	ret_info->value_ranges = value_ranges;
	tmp->name = show_ranges(ret_info->value_ranges);
	return tmp;
}

struct sm_state *__extra_pre_loop_hook_before(struct statement *iterator_pre_statement)
{
	struct expression *expr;
	char *name;
	struct symbol *sym;
	struct sm_state *ret = NULL;

	if (!iterator_pre_statement)
		return NULL;
 	if (iterator_pre_statement->type != STMT_EXPRESSION)
		return NULL;
	expr = iterator_pre_statement->expression;
	if (expr->type != EXPR_ASSIGNMENT)
		return NULL;
	name = get_variable_from_expr(expr->left, &sym);
	if (!name || !sym)
		goto free;
	ret = get_sm_state(my_id, name, sym);
free:
	free_string(name);
	return ret;
}

static const char *get_iter_op(struct expression *expr)
{
	if (expr->type != EXPR_POSTOP && expr->type != EXPR_PREOP)
		return NULL;
        return show_special(expr->op);
}

int __iterator_unchanged(struct sm_state *sm, struct statement *iterator)
{
	struct expression *iter_expr;
	const char *op;
	char *name;
	struct symbol *sym;
	int ret = 0;

	if (!iterator)
		return 0;
	if (iterator->type != STMT_EXPRESSION)
		return 0;
	iter_expr = iterator->expression;
        op = get_iter_op(iter_expr);
	if (!op || (strcmp(op, "--") && strcmp(op, "++")))
		return 0;
	name = get_variable_from_expr(iter_expr->unop, &sym);
	if (!name || !sym)
		goto free;
	if (get_sm_state(my_id, name, sym) == sm)
		ret = 1;
free:
	free_string(name);
	return ret;
}

void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition)
{
	struct expression *iter_expr;
	char *name;
	struct symbol *sym;
	long long value;
	int left;
	const char *op;
	struct smatch_state *state;
	struct data_info *dinfo;
	long long min, max;

	iter_expr = iterator->expression;

	if (condition->type != EXPR_COMPARE)
		return;
	value = get_value(condition->left);
	if (value == UNDEFINED) {
		value = get_value(condition->right);
		if (value == UNDEFINED)
			return;
		left = 1;
	}
	if (left)
		name = get_variable_from_expr(condition->left, &sym);
	else 
		name = get_variable_from_expr(condition->right, &sym);
	if (!name || !sym)
		goto free;
	if (sym != sm->sym || strcmp(name, sm->name))
		goto free;
	op = get_iter_op(iter_expr);
	state = get_state(my_id, name, sym);
	dinfo = (struct data_info *)state->data;
	min = get_dinfo_min(dinfo);
	max = get_dinfo_max(dinfo);
	if (!strcmp(op, "++") && min != whole_range.min && max == whole_range.max) {
		set_state(my_id, name, sym, alloc_extra_state(min));
	} else if (min == whole_range.min && max != whole_range.max) {
		set_state(my_id, name, sym, alloc_extra_state(max));
	}
free:
	free_string(name);
	return;
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
				set_state(my_id, name, sym, extra_undefined());
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
	set_state(my_id, name, sym, alloc_extra_state(get_value(expr->right)));
	free_string(name);
}

static void undef_expr(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	if (expr->op == '*')
		return;
	if (expr->op == '(')
		return;
	
	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		return;
	if (!get_state(my_id, name, sym)) {
		free_string(name);
		return;
	}
	set_state(my_id, name, sym, extra_undefined());
	free_string(name);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if (sym->ident) {
		name = sym->ident->name;
		if (sym->initializer) {
			set_state(my_id, name, sym, alloc_extra_state(get_value(sym->initializer)));
			scoped_state(name, my_id, sym);
		} else {
			set_state(my_id, name, sym, extra_undefined());
			scoped_state(name, my_id, sym);
		}
	}
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	cur_func = sym;
	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		if (!arg->ident) {
			continue;
		}
		set_state(my_id, arg->ident->name, arg, extra_undefined());
	} END_FOR_EACH_PTR(arg);
}

#define VAL_SINGLE 0
#define VAL_MAX    1
#define VAL_MIN    2

static long long get_implied_value_helper(struct expression *expr, int what)
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
	state = get_state(my_id, name, sym);
	free_string(name);
	if (!state || !state->data)
		return UNDEFINED;
	if (what == VAL_SINGLE)
		return get_single_value_from_range((struct data_info *)state->data);
	if (what == VAL_MAX)
		return get_dinfo_max((struct data_info *)state->data);
	return get_dinfo_min((struct data_info *)state->data);
}

int get_implied_single_val(struct expression *expr)
{
	return get_implied_value_helper(expr, VAL_SINGLE);
}

int get_implied_max(struct expression *expr)
{
	long long ret;

	ret = get_implied_value_helper(expr, VAL_MAX);
	if (ret == whole_range.max)
		return UNDEFINED;
	return ret;
}

int get_implied_min(struct expression *expr)
{
	long long ret;

	ret = get_implied_value_helper(expr, VAL_MIN);
	if (ret == whole_range.min)
		return UNDEFINED;
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
	long long value;
	char *name = NULL;
	struct symbol *sym;
	struct smatch_state *one_state;
	struct smatch_state *two_state;
	struct smatch_state *orig;
	int left;
	int comparison = expr->op;

	value = get_value(expr->left);
	if (value == UNDEFINED) {
		value = get_value(expr->right);
		if (value == UNDEFINED)
			return;
		left = 1;
	}
	if (left && expr->left->type == EXPR_CALL) {
		function_comparison(comparison, expr->left, value, left);
		return;
	}
	if (!left && expr->right->type == EXPR_CALL) {
		function_comparison(comparison, expr->right, value, left);
		return;
	}
	if (left)
		name = get_variable_from_expr(expr->left, &sym);
	else 
		name = get_variable_from_expr(expr->right, &sym);
	if (!name || !sym)
		goto free;

	orig = get_state(my_id, name, sym);
	if (!orig)
		orig = extra_undefined();

	switch(comparison){
	case '<':
	case SPECIAL_UNSIGNED_LT:
		one_state = filter_range(orig, whole_range.min, value - 1);
		two_state = filter_range(orig, value, whole_range.max); 
		if (left)		
			set_true_false_states(my_id, name, sym, two_state, one_state);
		else
			set_true_false_states(my_id, name, sym, one_state, two_state);
		return;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		one_state = filter_range(orig, whole_range.min, value);
		two_state = filter_range(orig, value + 1, whole_range.max); 
		if (left)		
			set_true_false_states(my_id, name, sym, two_state, one_state);
		else
			set_true_false_states(my_id, name, sym, one_state, two_state);
		return;
	case SPECIAL_EQUAL:
		// todo.  print a warning here for impossible conditions.
		one_state = alloc_extra_state(value);
		two_state = filter_range(orig, value, value); 
		set_true_false_states(my_id, name, sym, one_state, two_state);
		return;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		one_state = filter_range(orig, whole_range.min, value - 1);
		two_state = filter_range(orig, value, whole_range.max); 
		if (left)		
			set_true_false_states(my_id, name, sym, one_state, two_state);
		else
			set_true_false_states(my_id, name, sym, two_state, one_state);
		return;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		one_state = filter_range(orig, whole_range.min, value);
		two_state = filter_range(orig, value + 1, whole_range.max); 
		if (left)		
			set_true_false_states(my_id, name, sym, one_state, two_state);
		else
			set_true_false_states(my_id, name, sym, two_state, one_state);
		return;
	case SPECIAL_NOTEQUAL:
		one_state = alloc_extra_state(value);
		two_state = filter_range(orig, value, value); 
		set_true_false_states(my_id, name, sym, two_state, one_state);
		return;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		return;
	}
	return;
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
	case EXPR_CALL:
		function_comparison(SPECIAL_NOTEQUAL, expr, 0, 1);
		return;
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		name = get_variable_from_expr(expr, &sym);
		if (!name)
			return;
		pre_state = get_state(my_id, name, sym);
		true_state = add_filter(pre_state, 0);
		false_state = alloc_extra_state(0);
		set_true_false_states(my_id, name, sym, true_state, false_state);
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
	state = get_state(my_id, name, sym);
	if (!state || !state->data)
		goto exit;
	ret = !possibly_false(SPECIAL_NOTEQUAL, (struct data_info *)state->data, 0, 1);
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

static int do_comparison_range(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	struct smatch_state *state;
	long long value;
	int left = 0;
	int poss_true, poss_false;

	value = get_value(expr->left);
	if (value == UNDEFINED) {
		value = get_value(expr->right);
		if (value == UNDEFINED)
			return 3;
		left = 1;
	}
	if (left)
		name = get_variable_from_expr(expr->left, &sym);
	else 
		name = get_variable_from_expr(expr->right, &sym);
	if (!name || !sym)
		goto free;
	state = get_state(SMATCH_EXTRA, name, sym);
	if (!state)
		goto free;
	poss_true = possibly_true(expr->op, (struct data_info *)state->data, value, left);
	poss_false = possibly_false(expr->op, (struct data_info *)state->data, value, left);
	if (!poss_true && !poss_false)
		return 0;
	if (poss_true && !poss_false)
		return 1;
	if (!poss_true && poss_false)
		return 2;
	if (poss_true && poss_false)
		return 3;
free:
	free_string(name);
	return 3;
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
		if (do_comparison_range(expr) == 1)
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
		if (do_comparison_range(expr) == 2)
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
