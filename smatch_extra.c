/*
 * sparse/smatch_extra.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * smatch_extra.c is supposed to track the value of every variable.
 */

#include <stdlib.h>
#ifndef __USE_ISOC99
#define __USE_ISOC99 
#endif
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

static struct data_info *alloc_dinfo(void)
{
	struct data_info *ret;

	ret = __alloc_data_info(0);
	ret->equiv = NULL;
	ret->type = DATA_RANGE;
	ret->value_ranges = NULL;
	return ret;
}

static struct data_info *alloc_dinfo_range(long long min, long long max)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	add_range(&ret->value_ranges, min, max);
	return ret;
}

static struct data_info *alloc_dinfo_range_list(struct range_list *rl)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	ret->value_ranges = rl;
	return ret;
}

static struct data_info *clone_dinfo(struct data_info *dinfo)
{
	struct data_info *ret;

	ret = alloc_dinfo();
	ret->equiv = clone_tracker_list(dinfo->equiv);
	ret->value_ranges = clone_range_list(dinfo->value_ranges);
	return ret;
}

static struct smatch_state *clone_extra_state(struct smatch_state *state)
{
	struct smatch_state *ret;

	ret = __alloc_smatch_state(0);
	ret->name = state->name;
	ret->data = clone_dinfo(get_dinfo(state));
	return ret;
}

static struct smatch_state *alloc_extra_state_empty(void)
{
	struct smatch_state *state;
	struct data_info *dinfo;

	dinfo = alloc_dinfo();
	state = __alloc_smatch_state(0);
	state->data = dinfo;
	state->name = "";
	return state;
}

static struct smatch_state *alloc_extra_state_no_name(int val)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range(val, val);
	return state;
}

/* We do this because ->value_ranges is a list */
struct smatch_state *extra_undefined(void)
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

struct smatch_state *alloc_extra_state(long long val)
{
	struct smatch_state *state;

	state = alloc_extra_state_no_name(val);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

struct smatch_state *alloc_extra_state_range(long long min, long long max)
{
	struct smatch_state *state;

	if (min == whole_range.min && max == whole_range.max)
		return extra_undefined();
	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range(min, max);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

struct smatch_state *alloc_extra_state_range_list(struct range_list *rl)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_dinfo_range_list(rl);
	state->name = show_ranges(get_dinfo(state)->value_ranges);
	return state;
}

static void add_equiv(struct smatch_state *state, const char *name, struct symbol *sym)
{
	struct data_info *dinfo;

	dinfo = get_dinfo(state);
	add_tracker(&dinfo->equiv, SMATCH_EXTRA, name, sym);
}

static void del_equiv(struct smatch_state *state, const char *name, struct symbol *sym)
{
	struct data_info *dinfo;

	dinfo = get_dinfo(state);
	del_tracker(&dinfo->equiv, SMATCH_EXTRA, name, sym);
}

static void remove_from_equiv(const char *name, struct symbol *sym)
{
	struct sm_state *orig_sm;
	struct tracker *tracker;
	struct smatch_state *state;
	struct tracker_list *to_update;

	orig_sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!orig_sm || !get_dinfo(orig_sm->state)->equiv)
		return;

	state = clone_extra_state(orig_sm->state);
	del_equiv(state, name, sym);
	to_update = get_dinfo(state)->equiv;
	if (ptr_list_size((struct ptr_list *)get_dinfo(state)->equiv) == 1)
		get_dinfo(state)->equiv = NULL;

	FOR_EACH_PTR(to_update, tracker) {
		struct sm_state *new_sm;

		new_sm = clone_sm(orig_sm);
		new_sm->name = tracker->name;
		new_sm->sym = tracker->sym;
		new_sm->state = state;
		__set_sm(new_sm);
	} END_FOR_EACH_PTR(tracker);
}

static void remove_from_equiv_expr(struct expression *expr)
{
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	remove_from_equiv(name, sym);
free:
	free_string(name);
}

struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state)
{
	remove_from_equiv(name, sym);
	return set_state(SMATCH_EXTRA, name, sym, state);
}

struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state)
{
	remove_from_equiv_expr(expr);
	return set_state_expr(SMATCH_EXTRA, expr, state);
}

/*
 * This is for return_implies_state() hooks which modify a SMATCH_EXTRA state
 */
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state)
{
	struct tracker *tracker;
	struct smatch_state *orig_state;

	orig_state = get_state_expr(SMATCH_EXTRA, expr);

	if (!orig_state || !get_dinfo(orig_state)->equiv) {
		set_state_expr(SMATCH_EXTRA, expr, state);
		return;
	}

	FOR_EACH_PTR(get_dinfo(orig_state)->equiv, tracker) {
		set_state(tracker->owner, tracker->name, tracker->sym, state);
		add_equiv(state, tracker->name, tracker->sym);
	} END_FOR_EACH_PTR(tracker);
}

void set_extra_true_false(const char *name, struct symbol *sym,
			struct smatch_state *true_state,
			struct smatch_state *false_state)
{
	struct tracker *tracker;
	struct smatch_state *orig_state;

	orig_state = get_state(SMATCH_EXTRA, name, sym);

	if (!orig_state || !get_dinfo(orig_state)->equiv) {
		set_true_false_states(SMATCH_EXTRA, name, sym, true_state, false_state);
		return;
	}

	FOR_EACH_PTR(get_dinfo(orig_state)->equiv, tracker) {
		set_true_false_states(tracker->owner, tracker->name, tracker->sym, 
				true_state, false_state);
		if (true_state)
			add_equiv(true_state, tracker->name, tracker->sym);
		if (false_state)
			add_equiv(false_state, tracker->name, tracker->sym);
	} END_FOR_EACH_PTR(tracker);
}

struct data_info *get_dinfo(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return (struct data_info *)state->data;

}

struct smatch_state *filter_range(struct smatch_state *orig,
				 long long filter_min, long long filter_max)
{
	struct smatch_state *ret;
	struct data_info *orig_info;
	struct data_info *ret_info;

	if (!orig)
		orig = extra_undefined();
	orig_info = get_dinfo(orig);
	ret = alloc_extra_state_empty();
	ret_info = get_dinfo(ret);
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
	struct data_info *info1 = get_dinfo(s1);
	struct data_info *info2 = get_dinfo(s2);
	struct data_info *ret_info;
	struct smatch_state *tmp;
	struct range_list *value_ranges;
	struct tracker *tracker;

	value_ranges = range_list_union(info1->value_ranges, info2->value_ranges);
	tmp = alloc_extra_state_empty();
	ret_info = get_dinfo(tmp);
	ret_info->value_ranges = value_ranges;
	tmp->name = show_ranges(ret_info->value_ranges);
	FOR_EACH_PTR(info1->equiv, tracker) {
		if (in_tracker_list(info2->equiv, tracker->owner, tracker->name, tracker->sym))
			add_equiv(tmp, tracker->name, tracker->sym);
	} END_FOR_EACH_PTR(tracker);
	return tmp;
}

static struct sm_state *handle_canonical_while_count_down(struct statement *loop)
{
	struct expression *iter_var;
	struct expression *condition;
	struct sm_state *sm;
	long long start;

	condition = strip_expr(loop->iterator_pre_condition);
	if (!condition)
		return NULL;
	if (condition->type != EXPR_PREOP && condition->type != EXPR_POSTOP)
		return NULL;
	if (condition->op != SPECIAL_DECREMENT)
		return NULL;

	iter_var = condition->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (get_dinfo_min(get_dinfo(sm->state)) < 0)
		return NULL;
	start = get_dinfo_max(get_dinfo(sm->state));
	if  (start <= 0)
		return NULL;
	if (start != whole_range.max)
		start--;

	if (condition->type == EXPR_PREOP)
		set_extra_expr_mod(iter_var, alloc_extra_state_range(1, start));
	if (condition->type == EXPR_POSTOP)
		set_extra_expr_mod(iter_var, alloc_extra_state_range(0, start));
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_loops(struct statement *loop)
{
	struct expression *iter_expr;
	struct expression *iter_var;
	struct expression *condition;
	struct sm_state *sm;
	long long start;
	long long end;

	if (!loop->iterator_post_statement)
		return NULL;
	if (loop->iterator_post_statement->type != STMT_EXPRESSION)
		return NULL;
	iter_expr = loop->iterator_post_statement->expression;
	if (!loop->iterator_pre_condition)
		return NULL;
	if (loop->iterator_pre_condition->type != EXPR_COMPARE)
		return NULL;
	condition = loop->iterator_pre_condition;


	if (iter_expr->op != SPECIAL_INCREMENT)
		return NULL;
	iter_var = iter_expr->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (!get_single_value_from_dinfo(get_dinfo(sm->state), &start))
		return NULL;
	if (!get_implied_value(condition->right, &end))
		end = whole_range.max;
	if (get_sm_state_expr(SMATCH_EXTRA, condition->left) != sm)
		return NULL;

	switch (condition->op) {
	case SPECIAL_NOTEQUAL:
	case '<':
		if (end != whole_range.max)
			end--;
		break;
	case SPECIAL_LTE:
		break;
	default:
		return NULL;
	}
	if (end < start)
		return NULL;
	set_extra_expr_mod(iter_var, alloc_extra_state_range(start, end));
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct state_list **slist)
{
	struct sm_state *ret;

	__push_fake_cur_slist();;
	if (!loop->iterator_post_statement)
		ret = handle_canonical_while_count_down(loop);
	else
		ret = handle_canonical_for_loops(loop);
	*slist = __pop_fake_cur_slist();
	return ret;
}

int __iterator_unchanged(struct sm_state *sm)
{
	if (!sm)
		return 0;
	if (get_sm_state(my_id, sm->name, sm->sym) == sm)
		return 1;
	return 0;
}

static void while_count_down_after(struct sm_state *sm, struct expression *condition)
{
	long long after_value;

	/* paranoid checking.  prolly not needed */
	condition = strip_expr(condition);
	if (!condition)
		return;
	if (condition->type != EXPR_PREOP && condition->type != EXPR_POSTOP)
		return;
	if (condition->op != SPECIAL_DECREMENT)
		return;
	after_value = get_dinfo_min(get_dinfo(sm->state));
	after_value--;
	set_extra_mod(sm->name, sm->sym, alloc_extra_state(after_value));
}

void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition)
{
	struct expression *iter_expr;
	char *name;
	struct symbol *sym;
	long long value;
	int left = 0;
	struct smatch_state *state;
	struct data_info *dinfo;
	long long min, max;

	if (!iterator) {
		while_count_down_after(sm, condition);
		return;
	}

	iter_expr = iterator->expression;

	if (condition->type != EXPR_COMPARE)
		return;
	if (!get_value(condition->left, &value)) {
		if (!get_value(condition->right, &value))
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
	state = get_state(my_id, name, sym);
	dinfo = get_dinfo(state);
	min = get_dinfo_min(dinfo);
	max = get_dinfo_max(dinfo);
	if (iter_expr->op == SPECIAL_INCREMENT && min != whole_range.min && max == whole_range.max) {
		set_extra_mod(name, sym, alloc_extra_state(min));
	} else if (min == whole_range.min && max != whole_range.max) {
		set_extra_mod(name, sym, alloc_extra_state(max));
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
		if (tmp->type == EXPR_PREOP && tmp->op == '&') {
			name = get_variable_from_expr(tmp->unop, &sym);
			if (name) {
				set_extra_mod(name, sym, extra_undefined());
			}
			free_string(name);
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void set_equiv(struct sm_state *right_sm, struct expression *left)
{
	struct smatch_state *state;
	struct data_info *dinfo;
	struct tracker *tracker;
	char *name;
	struct symbol *sym;

	name = get_variable_from_expr(left, &sym);
	if (!name || !sym)
		goto free;

	remove_from_equiv(name, sym);

	state = clone_extra_state(right_sm->state);
	dinfo = get_dinfo(state);
	if (!dinfo->equiv)
		add_equiv(state, right_sm->name, right_sm->sym);
	add_equiv(state, name, sym);

	FOR_EACH_PTR(dinfo->equiv, tracker) {
		struct sm_state *new_sm;

		new_sm = clone_sm(right_sm);
		new_sm->name = tracker->name;
		new_sm->sym = tracker->sym;
		new_sm->state = state;
		__set_sm(new_sm);
	} END_FOR_EACH_PTR(tracker);
free:
	free_string(name);
}

static void match_assign(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct sm_state *right_sm;
	struct symbol *sym;
	char *name;
	long long value;
	int known;
	long long min = whole_range.min;
	long long max = whole_range.max;
	long long tmp;
	struct range_list *rl = NULL;
	
	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name)
		return;
	right = strip_expr(expr->right);
	while (right->type == EXPR_ASSIGNMENT && right->op == '=')
		right = strip_expr(right->left);

	right_sm = get_sm_state_expr(SMATCH_EXTRA, right);
	if (expr->op == '=' && right_sm) {
		set_equiv(right_sm, left);
		goto free;
	}

	known = get_implied_range_list(right, &rl);
	if (expr->op == '=') {
		if (known) 
			set_extra_mod(name, sym, alloc_extra_state_range_list(rl));
		else
			set_extra_mod(name, sym, extra_undefined());
		goto free;
	}

	known = get_implied_value(right, &value);
	if (expr->op == SPECIAL_ADD_ASSIGN) {
		if (get_implied_min(left, &tmp)) {
			if (known)
				min = tmp + value;
			else
				min = tmp;
		}
		if (!inside_loop() && known && get_implied_max(left, &tmp))
				max = tmp + value;
	}
	if (expr->op == SPECIAL_SUB_ASSIGN) {
		if (get_implied_max(left, &tmp)) {
			if (known)
				max = tmp - value;
			else
				max = tmp;
		}
		if (!inside_loop() && known && get_implied_min(left, &tmp))
				min = tmp - value;
	}
	set_extra_mod(name, sym, alloc_extra_state_range(min, max));
free:
	free_string(name);
}

static void unop_expr(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	long long min = whole_range.min;
	long long max = whole_range.max;
	long long val;

	if (expr->op == '*')
		return;
	if (expr->op == '(')
		return;
	if (expr->op == '!')
		return;
	
	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		goto free;
	if (expr->op == SPECIAL_INCREMENT) {
		if (get_implied_min(expr->unop, &val))
			min = val + 1;
		if (!inside_loop() && get_implied_max(expr->unop, &val))
			max = val + 1;
	}
	if (expr->op == SPECIAL_DECREMENT) {
		if (get_implied_max(expr->unop, &val))
			max = val - 1;
		if (!inside_loop() && get_implied_min(expr->unop, &val))
			min = val - 1;
	}
	set_extra_mod(name, sym, alloc_extra_state_range(min, max));
free:
	free_string(name);
}

static void delete_state_tracker(struct tracker *t)
{
	remove_from_equiv(t->name, t->sym);
	delete_state(t->owner, t->name, t->sym);
}

static void scoped_state_extra(const char *name, struct symbol *sym)
{
	struct tracker *t;

	t = alloc_tracker(SMATCH_EXTRA, name, sym);
	add_scope_hook((scope_hook *)&delete_state_tracker, t); 
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if (sym->ident) {
		name = sym->ident->name;
		if (!sym->initializer) {
			set_state(SMATCH_EXTRA, name, sym, extra_undefined());
			scoped_state_extra(name, sym);
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

static int last_stmt_val(struct statement *stmt, long long *val)
{
	struct expression *expr;

	if (!stmt)
		return 0;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return 0;
	expr = stmt->expression;
	return get_value(expr, val);
}

static void match_comparison(struct expression *expr)
{
	long long fixed;
	char *name = NULL;
	struct symbol *sym;
	struct smatch_state *true_state;
	struct smatch_state *false_state;
	struct smatch_state *orig;
	int left = 0;
	int comparison = expr->op;
	struct expression *varies = expr->right;

	if (!get_value(expr->left, &fixed)) { 
		if (!get_value(expr->right, &fixed))
			return;
		varies = strip_expr(expr->left);
		left = 1;
	}
	if (varies->op == SPECIAL_INCREMENT || varies->op == SPECIAL_DECREMENT) 
		varies = varies->unop;
	if (varies->type == EXPR_CALL) {
		function_comparison(comparison, varies, fixed, left);
                return;
	}

	name = get_variable_from_expr(varies, &sym);
	if (!name || !sym)
		goto free;

	orig = get_state(my_id, name, sym);
	if (!orig)
		orig = extra_undefined();

	switch (comparison) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left) {
			true_state = filter_range(orig, fixed, whole_range.max);
			false_state = filter_range(orig, whole_range.min, fixed - 1);
		} else {
			true_state = filter_range(orig, whole_range.min, fixed);
			false_state = filter_range(orig, fixed + 1, whole_range.max); 
		}
		break;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left) {
			true_state = filter_range(orig, fixed + 1, whole_range.max);
			false_state = filter_range(orig, whole_range.min, fixed);
		} else { 
			true_state = filter_range(orig, whole_range.min, fixed - 1);
			false_state = filter_range(orig, fixed, whole_range.max);
		}
		break;
	case SPECIAL_EQUAL:
		if (possibly_true(SPECIAL_EQUAL, get_dinfo(orig), fixed, fixed))
			true_state = alloc_extra_state(fixed);
		else
			true_state = alloc_extra_state_empty();
		false_state = filter_range(orig, fixed, fixed);
		break;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left) {
			true_state = filter_range(orig, whole_range.min, fixed - 1);
			false_state = filter_range(orig, fixed, whole_range.max);
		} else {
			true_state = filter_range(orig, fixed + 1, whole_range.max);
			false_state = filter_range(orig, whole_range.min, fixed);
		}
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left) {
			true_state = filter_range(orig, whole_range.min, fixed);
			false_state = filter_range(orig, fixed + 1, whole_range.max);
		} else {
			true_state = filter_range(orig, fixed, whole_range.max);
			false_state = filter_range(orig, whole_range.min, fixed - 1);
		}
		break;
	case SPECIAL_NOTEQUAL:
		true_state = filter_range(orig, fixed, fixed); 
		if (possibly_true(SPECIAL_EQUAL, get_dinfo(orig), fixed, fixed))
			false_state = alloc_extra_state(fixed);
		else
			false_state = alloc_extra_state_empty();
		break;
	default:
		sm_msg("unhandled comparison %d\n", comparison);
		goto free;
	}
	set_extra_true_false(name, sym, true_state, false_state);
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
	switch (expr->type) {
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
		if (possibly_true(SPECIAL_EQUAL, get_dinfo(pre_state), 0, 0))
			false_state = alloc_extra_state(0);
		else
			false_state = alloc_extra_state_empty();
		set_extra_true_false(name, sym, true_state, false_state);
		free_string(name);
		return;
	case EXPR_COMPARE:
		match_comparison(expr);
		return;
	case EXPR_ASSIGNMENT:
		__extra_match_condition(expr->left);
		return;
	}
}

/* returns 1 if it is not possible for expr to be value, otherwise returns 0 */
int implied_not_equal(struct expression *expr, long long val)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	int ret = 0;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto exit;
	state = get_state(my_id, name, sym);
	if (!state || !state->data)
		goto exit;
	ret = !possibly_false(SPECIAL_NOTEQUAL, get_dinfo(state), val, 1);
exit:
	free_string(name);
	return ret;
}

int known_condition_true(struct expression *expr)
{
	long long tmp;
	struct statement *stmt;

	if (!expr)
		return 0;

	if (get_value(expr, &tmp) && tmp)
		return 1;
	
	expr = strip_expr(expr);
	switch (expr->type) {
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_false(expr->unop))
				return 1;
			break;
		}
		stmt = get_expression_statement(expr);
		if (last_stmt_val(stmt, &tmp) && tmp == 1)
			return 1;
		break;
	default:
		break;
	}
	return 0;
}

int known_condition_false(struct expression *expr)
{
	long long tmp;
	struct statement *stmt;

	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	switch (expr->type) {
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_true(expr->unop))
				return 1;
			break;
		}
		stmt = get_expression_statement(expr);
		if (last_stmt_val(stmt, &tmp) && tmp == 0)
			return 1;
		break;
	default:
		break;
	}
	return 0;
}

static struct range_list *get_range_list(struct expression *expr)
{
	long long min;
	long long max;
	struct range_list *ret = NULL;
	struct smatch_state *state;

	state = get_state_expr(SMATCH_EXTRA, expr);
	if (state)
		return clone_range_list(get_dinfo(state)->value_ranges);
	if (!get_absolute_min(expr, &min))
		return NULL;
	if (!get_absolute_max(expr, &max))
		return NULL;
	add_range(&ret, min, max);
	return ret;
}

static int do_comparison(struct expression *expr)
{
	struct range_list *left_ranges;
	struct range_list *right_ranges;
	int poss_true, poss_false;

	left_ranges = get_range_list(expr->left);
	right_ranges = get_range_list(expr->right);

	poss_true = possibly_true_range_lists(left_ranges, expr->op, right_ranges);
	poss_false = possibly_false_range_lists(left_ranges, expr->op, right_ranges);

	free_range_list(&left_ranges);
	free_range_list(&right_ranges);

	if (!poss_true && !poss_false)
		return 0;
	if (poss_true && !poss_false)
		return 1;
	if (!poss_true && poss_false)
		return 2;
	return 3;
}

int implied_condition_true(struct expression *expr)
{
	struct statement *stmt;
	long long tmp;
	long long val;

	if (!expr)
		return 0;

	if (get_implied_value(expr, &tmp) && tmp)
		return 1;

	if (expr->type == EXPR_POSTOP)
		return implied_condition_true(expr->unop);

	if (expr->type == EXPR_PREOP && expr->op == SPECIAL_DECREMENT)
		return implied_not_equal(expr->unop, 1);
	if (expr->type == EXPR_PREOP && expr->op == SPECIAL_INCREMENT)
		return implied_not_equal(expr->unop, -1);
	
	expr = strip_expr(expr);
	switch (expr->type) {
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
		stmt = get_expression_statement(expr);
		if (last_stmt_val(stmt, &val) && val == 1)
			return 1;
		break;
	default:
		if (implied_not_equal(expr, 0) == 1)
			return 1;
		break;
	}
	return 0;
}

int implied_condition_false(struct expression *expr)
{
	struct statement *stmt;
	struct expression *tmp;
	long long val;

	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	switch (expr->type) {
	case EXPR_COMPARE:
		if (do_comparison(expr) == 2)
			return 1;
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (implied_condition_true(expr->unop))
				return 1;
			break;
		}
		stmt = get_expression_statement(expr);
		if (last_stmt_val(stmt, &val) && val == 0)
			return 1;
		tmp = strip_expr(expr);
		if (tmp != expr)
			return implied_condition_false(tmp);
		break;
	default:
		if (get_implied_value(expr, &val) && val == 0)
			return 1;
		break;
	}
	return 0;
}

int get_implied_range_list(struct expression *expr, struct range_list **rl)
{
	long long val;
	struct smatch_state *state;

	expr = strip_expr(expr);

	state = get_state_expr(my_id, expr);
	if (state) {
		*rl = clone_range_list(get_dinfo(state)->value_ranges);
		return 1;
	}

	if (get_implied_value(expr, &val)) {
		*rl = NULL;
		add_range(rl, val, val);
		return 1;
	}

	if (expr->type == EXPR_BINOP && expr->op == '%') {
		if (!get_implied_value(expr->right, &val))
			return 0;
		*rl = NULL;
		add_range(rl, 0, val - 1);
		return 1;
	}

	return 0;
}

int is_whole_range(struct smatch_state *state)
{
	struct data_info *dinfo;
	struct data_range *drange;

	if (!state)
		return 0;
	dinfo = get_dinfo(state);
	drange = first_ptr_list((struct ptr_list *)dinfo->value_ranges);
	if (drange->min == whole_range.min && drange->max == whole_range.max)
		return 1;
	return 0;
}

void register_smatch_extra(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
}
