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
 *
 */

#include <stdlib.h>
#include <errno.h>
#ifndef __USE_ISOC99
#define __USE_ISOC99
#endif
#include <limits.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

struct string_list *__ignored_macros = NULL;
static int in_warn_on_macro()
{
	struct statement *stmt;
	char *tmp;
	char *macro;

	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	macro = get_macro_name(stmt->pos);
	if (!macro)
		return 0;

	FOR_EACH_PTR(__ignored_macros, tmp) {
		if (!strcmp(tmp, macro))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state)
{
	if (in_warn_on_macro())
		return NULL;
	remove_from_equiv(name, sym);
	return set_state(SMATCH_EXTRA, name, sym, state);
}

struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state)
{
	if (in_warn_on_macro())
		return NULL;
	remove_from_equiv_expr(expr);
	return set_state_expr(SMATCH_EXTRA, expr, state);
}

/*
 * This is for return_implies_state() hooks which modify a SMATCH_EXTRA state
 */
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state)
{
	struct relation *rel;
	struct smatch_state *orig_state;

	orig_state = get_state_expr(SMATCH_EXTRA, expr);

	if (!estate_related(orig_state)) {
		set_state_expr(SMATCH_EXTRA, expr, state);
		return;
	}

	FOR_EACH_PTR(estate_related(orig_state), rel) {
		if (option_debug_related)
			sm_msg("updating related %s to %s", rel->name, state->name);
		set_state(SMATCH_EXTRA, rel->name, rel->sym, state);
		add_equiv(state, rel->name, rel->sym);
	} END_FOR_EACH_PTR(rel);
}

static void set_extra_true_false(const char *name, struct symbol *sym,
			struct smatch_state *true_state,
			struct smatch_state *false_state)
{
	struct relation *rel;
	struct smatch_state *orig_state;

	if (in_warn_on_macro())
		return;

	orig_state = get_state(SMATCH_EXTRA, name, sym);

	if (!estate_related(orig_state)) {
		set_true_false_states(SMATCH_EXTRA, name, sym, true_state, false_state);
		return;
	}

	// FIXME!!!!  equiv => related
	FOR_EACH_PTR(estate_related(orig_state), rel) {
		set_true_false_states(SMATCH_EXTRA, rel->name, rel->sym,
				true_state, false_state);
		if (true_state)
			add_equiv(true_state, rel->name, rel->sym);
		if (false_state)
			add_equiv(false_state, rel->name, rel->sym);
	} END_FOR_EACH_PTR(rel);
}

static void set_extra_expr_true_false(struct expression *expr,
		struct smatch_state *true_state,
		struct smatch_state *false_state)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	set_extra_true_false(name, sym, true_state, false_state);
free:
	free_string(name);
}

struct smatch_state *filter_range(struct smatch_state *orig,
				 long long filter_min, long long filter_max)
{
	struct range_list *rl;

	if (!orig)
		orig = extra_undefined();

	rl = remove_range(estate_ranges(orig), filter_min, filter_max);
	return alloc_estate_range_list(rl);
}

struct smatch_state *add_filter(struct smatch_state *orig, long long num)
{
	return filter_range(orig, num, num);
}

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	struct smatch_state *tmp;
	struct range_list *value_ranges;

	value_ranges = range_list_union(estate_ranges(s1), estate_ranges(s2));
	tmp = alloc_estate_range_list(value_ranges);
	get_dinfo(tmp)->related = get_shared_relations(estate_related(s1),
						       estate_related(s2));
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
	if (estate_min(sm->state) < 0)
		return NULL;
	start = estate_max(sm->state);
	if  (start <= 0)
		return NULL;
	if (start != whole_range.max)
		start--;

	if (condition->type == EXPR_PREOP)
		set_extra_expr_mod(iter_var, alloc_estate_range(1, start));
	if (condition->type == EXPR_POSTOP)
		set_extra_expr_mod(iter_var, alloc_estate_range(0, start));
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_inc(struct expression *iter_expr,
						struct expression *condition)
{
	struct expression *iter_var;
	struct sm_state *sm;
	long long start;
	long long end;

	iter_var = iter_expr->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (!estate_get_single_value(sm->state, &start))
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
	set_extra_expr_mod(iter_var, alloc_estate_range(start, end));
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_dec(struct expression *iter_expr,
						struct expression *condition)
{
	struct expression *iter_var;
	struct sm_state *sm;
	long long start;
	long long end;

	iter_var = iter_expr->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (!estate_get_single_value(sm->state, &start))
		return NULL;
	if (!get_implied_value(condition->right, &end))
		end = whole_range.max;
	if (get_sm_state_expr(SMATCH_EXTRA, condition->left) != sm)
		return NULL;

	switch (condition->op) {
	case SPECIAL_NOTEQUAL:
	case '>':
		if (end != whole_range.min)
			end++;
		break;
	case SPECIAL_GTE:
		break;
	default:
		return NULL;
	}
	if (end > start)
		return NULL;
	set_extra_expr_mod(iter_var, alloc_estate_range(end, start));
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_loops(struct statement *loop)
{
	struct expression *iter_expr;
	struct expression *condition;

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

	if (iter_expr->op == SPECIAL_INCREMENT)
		return handle_canonical_for_inc(iter_expr, condition);
	if (iter_expr->op == SPECIAL_DECREMENT)
		return handle_canonical_for_dec(iter_expr, condition);
	return NULL;
}

struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct state_list **slist)
{
	struct sm_state *ret;

	__push_fake_cur_slist();
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
	after_value = estate_min(sm->state);
	after_value--;
	set_extra_mod(sm->name, sm->sym, alloc_estate(after_value));
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
	min = estate_min(state);
	max = estate_max(state);
	if (iter_expr->op == SPECIAL_INCREMENT &&
		min != whole_range.min &&
		max == whole_range.max) {
		set_extra_mod(name, sym, alloc_estate(min));
	} else if (min == whole_range.min && max != whole_range.max) {
		set_extra_mod(name, sym, alloc_estate(max));
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
	struct expression *arg;
	struct expression *tmp;

	FOR_EACH_PTR(expr->args, arg) {
		tmp = strip_expr(arg);
		if (tmp->type == EXPR_PREOP && tmp->op == '&') {
			remove_from_equiv_expr(tmp->unop);
			set_state_expr(SMATCH_EXTRA, tmp->unop, extra_undefined());
		}
	} END_FOR_EACH_PTR(arg);
}

static void match_assign(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	struct symbol *right_sym;
	char *right_name;
	struct symbol *sym;
	char *name;
	long long value;
	int known;
	long long min = whole_range.min;
	long long max = whole_range.max;
	long long tmp;

	if (__is_condition_assign(expr))
		return;
	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name)
		return;
	right = strip_parens(expr->right);
	while (right->type == EXPR_ASSIGNMENT && right->op == '=')
		right = strip_parens(right->left);

	if (expr->op == '=' && right->type == EXPR_CALL)
		return;  /* these are handled in match_call_assign() */

	right_name = get_variable_from_expr(right, &right_sym);
	if (expr->op == '=' && right_name && right_sym) {
		set_equiv(left, right);
		goto free;
	}

	known = get_implied_value(right, &value);
	switch (expr->op) {
	case '=': {
		struct range_list *rl = NULL;

		if (get_implied_range_list(right, &rl)) {
			set_extra_mod(name, sym, alloc_estate_range_list(rl));
			goto free;
		}

		if (expr_unsigned(right))
			min = 0;
		break;
	}
	case SPECIAL_ADD_ASSIGN:
		if (get_implied_min(left, &tmp)) {
			if (known)
				min = tmp + value;
			else
				min = tmp;
		}
		if (!inside_loop() && known && get_implied_max(left, &tmp))
				max = tmp + value;
		break;
	case SPECIAL_SUB_ASSIGN:
		if (get_implied_max(left, &tmp)) {
			if (known)
				max = tmp - value;
			else
				max = tmp;
		}
		if (!inside_loop() && known && get_implied_min(left, &tmp))
				min = tmp - value;
		break;
	}
	set_extra_mod(name, sym, alloc_estate_range(min, max));
free:
	free_string(right_name);
	free_string(name);
}

static void reset_struct_members(struct sm_state *sm)
{
	set_extra_mod(sm->name, sm->sym, extra_undefined());
}

static struct smatch_state *increment_state(struct smatch_state *state)
{
	long long min = estate_min(state);
	long long max = estate_max(state);

	if (inside_loop())
		max = whole_range.max;

	if (min != whole_range.min)
		min++;
	if (max != whole_range.max)
		max++;
	return alloc_estate_range(min, max);
}

static struct smatch_state *decrement_state(struct smatch_state *state)
{
	long long min = estate_min(state);
	long long max = estate_max(state);

	if (inside_loop())
		min = whole_range.min;

	if (min != whole_range.min)
		min--;
	if (max != whole_range.max)
		max--;
	return alloc_estate_range(min, max);
}

static void unop_expr(struct expression *expr)
{
	struct smatch_state *state;

	if (expr->smatch_flags & Handled)
		return;

	switch (expr->op) {
	case SPECIAL_INCREMENT:
		state = get_state_expr(SMATCH_EXTRA, expr->unop);
		state = increment_state(state);
		set_extra_expr_mod(expr->unop, state);
		break;
	case SPECIAL_DECREMENT:
		state = get_state_expr(SMATCH_EXTRA, expr->unop);
		state = decrement_state(state);
		set_extra_expr_mod(expr->unop, state);
		break;
	default:
		return;
	}
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

static void check_dereference(struct expression *expr)
{
	set_extra_expr_nomod(expr, alloc_estate_range(valid_ptr_min, valid_ptr_max));
}

static void match_dereferences(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	check_dereference(expr->unop);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	check_dereference(expr->unop->left);
}

static void set_param_dereferenced(struct expression *arg, char *unused)
{
	check_dereference(arg);
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		if (!arg->ident)
			continue;
		set_state(my_id, arg->ident->name, arg, extra_undefined());
	} END_FOR_EACH_PTR(arg);
}

static int match_func_comparison(struct expression *expr)
{
	struct expression *left = strip_expr(expr->left);
	struct expression *right = strip_expr(expr->right);
	long long val;

	if (left->type == EXPR_CALL) {
		if (!get_implied_value(right, &val))
			return 1;
		function_comparison(expr->op, left, val, 1);
		return 1;
	}

	if (right->type == EXPR_CALL) {
		if (!get_implied_value(left, &val))
			return 1;
		function_comparison(expr->op, right, val, 0);
		return 1;
	}

	return 0;
}

static void match_comparison(struct expression *expr)
{
	struct expression *left = strip_expr(expr->left);
	struct expression *right = strip_expr(expr->right);
	struct range_list *left_orig;
	struct range_list *left_true;
	struct range_list *left_false;
	struct range_list *right_orig;
	struct range_list *right_true;
	struct range_list *right_false;
	struct smatch_state *left_true_state;
	struct smatch_state *left_false_state;
	struct smatch_state *right_true_state;
	struct smatch_state *right_false_state;
	int left_postop = 0;
	int right_postop = 0;

	if (match_func_comparison(expr))
		return;

	if (left->op == SPECIAL_INCREMENT || left->op == SPECIAL_DECREMENT) {
		if (left->type == EXPR_POSTOP) {
			left->smatch_flags |= Handled;
			left_postop = left->op;
		}
		left = strip_expr(left->unop);
	}

	if (right->op == SPECIAL_INCREMENT || right->op == SPECIAL_DECREMENT) {
		if (right->type == EXPR_POSTOP) {
			right->smatch_flags |= Handled;
			right_postop = right->op;
		}
		right = strip_expr(right->unop);
	}

	if (!get_implied_range_list(left, &left_orig))
		left_orig = alloc_range_list(whole_range.min, whole_range.max);
	if (!get_implied_range_list(right, &right_orig))
		right_orig = alloc_range_list(whole_range.min, whole_range.max);

	left_true = clone_range_list(left_orig);
	left_false = clone_range_list(left_orig);
	right_true = clone_range_list(right_orig);
	right_false = clone_range_list(right_orig);

	switch (expr->op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (rl_max(right_orig) != whole_range.max)
			left_true = remove_range(left_orig, rl_max(right_orig), whole_range.max);
		if (rl_min(right_orig) != whole_range.min)
			left_false = remove_range(left_orig, whole_range.min, rl_min(right_orig) - 1);

		if (rl_min(left_orig) != whole_range.min)
			right_true = remove_range(right_orig, whole_range.min, rl_min(left_orig));
		if (rl_max(left_orig) != whole_range.max)
			right_false = remove_range(right_orig, rl_max(left_orig) + 1, whole_range.max);
		break;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (rl_max(right_orig) != whole_range.max)
			left_true = remove_range(left_orig, rl_max(right_orig) + 1, whole_range.max);
		if (rl_min(right_orig) != whole_range.min)
			left_false = remove_range(left_orig, whole_range.min, rl_min(right_orig));

		if (rl_min(left_orig) != whole_range.min)
			right_true = remove_range(right_orig, whole_range.min, rl_min(left_orig) - 1);
		if (rl_max(left_orig) != whole_range.max)
			right_false = remove_range(right_orig, rl_max(left_orig), whole_range.max);
		break;
	case SPECIAL_EQUAL:
		if (rl_max(right_orig) != whole_range.max)
			left_true = remove_range(left_true, rl_max(right_orig) + 1, whole_range.max);
		if (rl_min(right_orig) != whole_range.min)
			left_true = remove_range(left_true, whole_range.min, rl_min(right_orig) - 1);
		if (rl_min(right_orig) == rl_max(right_orig))
			left_false = remove_range(left_orig, rl_min(right_orig), rl_min(right_orig));

		if (rl_max(left_orig) != whole_range.max)
			right_true = remove_range(right_true, rl_max(left_orig) + 1, whole_range.max);
		if (rl_min(left_orig) != whole_range.min)
			right_true = remove_range(right_true, whole_range.min, rl_min(left_orig) - 1);
		if (rl_min(left_orig) == rl_max(left_orig))
			right_false = remove_range(right_orig, rl_min(left_orig), rl_min(left_orig));
		break;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (rl_min(right_orig) != whole_range.min)
			left_true = remove_range(left_orig, whole_range.min, rl_min(right_orig) - 1);
		if (rl_max(right_orig) != whole_range.max)
			left_false = remove_range(left_orig, rl_max(right_orig), whole_range.max);

		if (rl_max(left_orig) != whole_range.max)
			right_true = remove_range(right_orig, rl_max(left_orig) + 1, whole_range.max);
		if (rl_min(left_orig) != whole_range.min)
			right_false = remove_range(right_orig, whole_range.min, rl_min(left_orig));
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (rl_min(right_orig) != whole_range.min)
			left_true = remove_range(left_orig, whole_range.min, rl_min(right_orig));
		if (rl_max(right_orig) != whole_range.max)
			left_false = remove_range(left_orig, rl_max(right_orig) + 1, whole_range.max);

		if (rl_max(left_orig) != whole_range.max)
			right_true = remove_range(right_orig, rl_max(left_orig), whole_range.max);
		if (rl_min(left_orig) != whole_range.min)
			right_false = remove_range(right_orig, whole_range.min, rl_min(left_orig) - 1);
		break;
	case SPECIAL_NOTEQUAL:
		if (rl_max(right_orig) != whole_range.max)
			left_false = remove_range(left_false, rl_max(right_orig) + 1, whole_range.max);
		if (rl_min(right_orig) != whole_range.min)
			left_false = remove_range(left_false, whole_range.min, rl_min(right_orig) - 1);
		if (rl_min(right_orig) == rl_max(right_orig))
			left_true = remove_range(left_orig, rl_min(right_orig), rl_min(right_orig));

		if (rl_max(left_orig) != whole_range.max)
			right_false = remove_range(right_false, rl_max(left_orig) + 1, whole_range.max);
		if (rl_min(left_orig) != whole_range.min)
			right_false = remove_range(right_false, whole_range.min, rl_min(left_orig) - 1);
		if (rl_min(left_orig) == rl_max(left_orig))
			right_true = remove_range(right_orig, rl_min(left_orig), rl_min(left_orig));
		break;
	default:
		return;
	}

	left_true_state = alloc_estate_range_list(left_true);
	left_false_state = alloc_estate_range_list(left_false);
	right_true_state = alloc_estate_range_list(right_true);
	right_false_state = alloc_estate_range_list(right_false);

	if (left_postop == SPECIAL_INCREMENT) {
		left_true_state = increment_state(left_true_state);
		left_false_state = increment_state(left_false_state);
	}
	if (left_postop == SPECIAL_DECREMENT) {
		left_true_state = decrement_state(left_true_state);
		left_false_state = decrement_state(left_false_state);
	}
	if (right_postop == SPECIAL_INCREMENT) {
		right_true_state = increment_state(right_true_state);
		right_false_state = increment_state(right_false_state);
	}
	if (right_postop == SPECIAL_DECREMENT) {
		right_true_state = decrement_state(right_true_state);
		right_false_state = decrement_state(right_false_state);
	}

	set_extra_expr_true_false(left, left_true_state, left_false_state);
	set_extra_expr_true_false(right, right_true_state, right_false_state);
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
		if (possibly_true(expr, SPECIAL_EQUAL, zero_expr()))
			false_state = alloc_estate(0);
		else
			false_state = alloc_estate_empty();
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
	return !possibly_false(expr, SPECIAL_NOTEQUAL, value_expr(val));
}

int get_implied_range_list(struct expression *expr, struct range_list **rl)
{
	long long val;
	struct smatch_state *state;
	long long min;
	long long max;

	*rl = NULL;

	expr = strip_parens(expr);
	if (!expr)
		return 0;

	state = get_state_expr(my_id, expr);
	if (state) {
		*rl = clone_range_list(estate_ranges(state));
		return 1;
	}

	if (expr->type == EXPR_CALL) {
		*rl = db_return_vals(expr);
		return !!*rl;
	}

	if (get_implied_value(expr, &val)) {
		add_range(rl, val, val);
		return 1;
	}

	if (expr->type == EXPR_BINOP && expr->op == '%') {
		if (!get_implied_value(expr->right, &val))
			return 0;
		add_range(rl, 0, val - 1);
		return 1;
	}

	if (!get_implied_min(expr, &min))
		return 0;
	if (!get_implied_max(expr, &max))
		return 0;

	*rl = alloc_range_list(min, max);
	return 1;
}

int is_whole_range(struct smatch_state *state)
{
	return is_whole_range_rl(estate_ranges(state));
}

static void struct_member_callback(char *fn, char *global_static, int param, char *printed_name, struct smatch_state *state)
{
	if (is_whole_range(state))
		return;
	sm_msg("info: passes param_value '%s' %d '%s' %s %s", fn, param, printed_name, state->name, global_static);
}

static void match_call_info(struct expression *expr)
{
	struct range_list *rl = NULL;
	struct expression *arg;
	char *name;
	int i = 0;

	name = get_fnptr_name(expr->fn);
	if (!name)
		return;

	FOR_EACH_PTR(expr->args, arg) {
		if (get_implied_range_list(arg, &rl) && !is_whole_range_rl(rl)) {
			sm_msg("info: passes param_value '%s' %d '$$' %s %s",
			       name, i, show_ranges(rl),
			       is_static(expr->fn) ? "static" : "global");
		}
		i++;
	} END_FOR_EACH_PTR(arg);

	free_string(name);
}

void set_param_value(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	char fullname[256];

	if (strncmp(key, "$$", 2))
		return;

	snprintf(fullname, 256, "%s%s", name, key + 2);
	get_value_ranges(value, &rl);
	char *tmp = show_ranges(rl);
	if (strcmp(tmp, value))
		sm_msg("value = %s, ranges = %s", value, tmp);
	state = alloc_estate_range_list(rl);
	set_state(SMATCH_EXTRA, fullname, sym, state);
}

static void match_call_assign(struct expression *expr)
{
	struct expression *right;
	struct range_list *rl;

	if (expr->op != '=')
		return;

	right = strip_parens(expr->right);
	while (right->type == EXPR_ASSIGNMENT && right->op == '=')
		right = strip_parens(right->left);

	rl = db_return_vals(right);
	if (rl)
		set_extra_expr_mod(expr->left, alloc_estate_range_list(rl));
	else
		set_extra_expr_mod(expr->left, extra_undefined());
}

void register_smatch_extra(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	if (option_info) {
		add_hook(&match_call_info, FUNCTION_CALL_HOOK);
		add_member_info_callback(my_id, struct_member_callback);
	}
	add_definition_db_callback(set_param_value, PARAM_VALUE);
}

void register_smatch_extra_late(int id)
{
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	add_db_fn_call_callback(DEREFERENCE, &set_param_dereferenced);
	add_indirect_modification_hook(SMATCH_EXTRA, reset_struct_members);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_call_assign, CALL_ASSIGNMENT_HOOK);

}
