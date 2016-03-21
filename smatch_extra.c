/*
 * Copyright (C) 2008 Dan Carpenter.
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
static int link_id;

static void match_link_modify(struct sm_state *sm, struct expression *mod_expr);

struct string_list *__ignored_macros = NULL;
static int in_warn_on_macro(void)
{
	struct statement *stmt;
	char *tmp;
	char *macro;

	stmt = get_current_statement();
	if (!stmt)
		return 0;
	macro = get_macro_name(stmt->pos);
	if (!macro)
		return 0;

	FOR_EACH_PTR(__ignored_macros, tmp) {
		if (!strcmp(tmp, macro))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

typedef void (mod_hook)(const char *name, struct symbol *sym, struct smatch_state *state);
DECLARE_PTR_LIST(void_fn_list, mod_hook *);
static struct void_fn_list *extra_mod_hooks;
void add_extra_mod_hook(mod_hook *fn)
{
	mod_hook **p = malloc(sizeof(mod_hook *));
	*p = fn;
	add_ptr_list(&extra_mod_hooks, p);
}

void call_extra_mod_hooks(const char *name, struct symbol *sym, struct smatch_state *state)
{
	mod_hook **fn;

	FOR_EACH_PTR(extra_mod_hooks, fn) {
		(*fn)(name, sym, state);
	} END_FOR_EACH_PTR(fn);
}

struct sm_state *set_extra_mod_helper(const char *name, struct symbol *sym, struct smatch_state *state)
{
	remove_from_equiv(name, sym);
	call_extra_mod_hooks(name, sym, state);
	if (__in_fake_assign && estate_is_unknown(state) && !get_state(SMATCH_EXTRA, name, sym))
		return NULL;
	return set_state(SMATCH_EXTRA, name, sym, state);
}

static char *get_other_name_sym(const char *name, struct symbol *sym, struct symbol **new_sym)
{
	struct expression *assigned;
	char *orig_name = NULL;
	char buf[256];
	char *ret = NULL;
	int skip;

	*new_sym = NULL;

	if (!sym->ident)
		return NULL;

	skip = strlen(sym->ident->name);
	if (name[skip] != '-' || name[skip + 1] != '>')
		return NULL;
	skip += 2;

	assigned = get_assigned_expr_name_sym(sym->ident->name, sym);
	if (!assigned)
		return NULL;
	if (assigned->type == EXPR_PREOP || assigned->op == '&') {

		orig_name = expr_to_var_sym(assigned, new_sym);
		if (!orig_name || !*new_sym)
			goto free;

		snprintf(buf, sizeof(buf), "%s.%s", orig_name + 1, name + skip);
		ret = alloc_string(buf);
		free_string(orig_name);
		return ret;
	}

	if (assigned->type != EXPR_DEREF)
		goto free;

	orig_name = expr_to_var_sym(assigned, new_sym);
	if (!orig_name || !*new_sym)
		goto free;

	snprintf(buf, sizeof(buf), "%s->%s", orig_name, name + skip);
	ret = alloc_string(buf);
	free_string(orig_name);
	return ret;

free:
	free_string(orig_name);
	return NULL;
}

struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state)
{
	char *new_name;
	struct symbol *new_sym;
	struct sm_state *sm;

	sm = set_extra_mod_helper(name, sym, state);
	new_name = get_other_name_sym(name, sym, &new_sym);
	if (new_name && new_sym)
		set_extra_mod_helper(new_name, new_sym, state);
	free_string(new_name);
	return sm;
}

static void clear_array_states(struct expression *array)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(link_id, array);
	if (sm)
		match_link_modify(sm, NULL);
}

static struct sm_state *set_extra_array_mod(struct expression *expr, struct smatch_state *state)
{
	struct expression *array;
	struct var_sym_list *vsl;
	struct var_sym *vs;
	char *name;
	struct symbol *sym;
	struct sm_state *ret = NULL;

	array = get_array_base(expr);

	name = expr_to_chunk_sym_vsl(expr, &sym, &vsl);
	if (!name || !vsl) {
		clear_array_states(array);
		goto free;
	}

	FOR_EACH_PTR(vsl, vs) {
		store_link(link_id, vs->var, vs->sym, name, sym);
	} END_FOR_EACH_PTR(vs);

	call_extra_mod_hooks(name, sym, state);
	ret = set_state(SMATCH_EXTRA, name, sym, state);
free:
	free_string(name);
	return ret;
}

struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state)
{
	struct symbol *sym;
	char *name;
	struct sm_state *ret = NULL;

	if (is_array(expr))
		return set_extra_array_mod(expr, state);

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	ret = set_extra_mod(name, sym, state);
free:
	free_string(name);
	return ret;
}

void set_extra_nomod(const char *name, struct symbol *sym, struct smatch_state *state)
{
	char *new_name;
	struct symbol *new_sym;
	struct relation *rel;
	struct smatch_state *orig_state;

	orig_state = get_state(SMATCH_EXTRA, name, sym);

	new_name = get_other_name_sym(name, sym, &new_sym);
	if (new_name && new_sym)
		set_state(SMATCH_EXTRA, new_name, new_sym, state);
	free_string(new_name);

	if (!estate_related(orig_state)) {
		set_state(SMATCH_EXTRA, name, sym, state);
		return;
	}

	set_related(state, estate_related(orig_state));
	FOR_EACH_PTR(estate_related(orig_state), rel) {
		struct smatch_state *estate;

		if (option_debug_related)
			sm_msg("%s updating related %s to %s", name, rel->name, state->name);
		estate = get_state(SMATCH_EXTRA, rel->name, rel->sym);
		if (!estate)
			continue;
		set_state(SMATCH_EXTRA, rel->name, rel->sym, clone_estate_cast(estate_type(estate), state));
	} END_FOR_EACH_PTR(rel);
}

/*
 * This is for return_implies_state() hooks which modify a SMATCH_EXTRA state
 */
void set_extra_expr_nomod(struct expression *expr, struct smatch_state *state)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	set_extra_nomod(name, sym, state);
free:
	free_string(name);

}

static void set_extra_true_false(const char *name, struct symbol *sym,
			struct smatch_state *true_state,
			struct smatch_state *false_state)
{
	char *new_name;
	struct symbol *new_sym;
	struct relation *rel;
	struct smatch_state *orig_state;

	if (!true_state && !false_state)
		return;

	if (in_warn_on_macro())
		return;

	new_name = get_other_name_sym(name, sym, &new_sym);
	if (new_name && new_sym)
		set_true_false_states(SMATCH_EXTRA, new_name, new_sym, true_state, false_state);
	free_string(new_name);

	orig_state = get_state(SMATCH_EXTRA, name, sym);

	if (!estate_related(orig_state)) {
		set_true_false_states(SMATCH_EXTRA, name, sym, true_state, false_state);
		return;
	}

	if (true_state)
		set_related(true_state, estate_related(orig_state));
	if (false_state)
		set_related(false_state, estate_related(orig_state));

	FOR_EACH_PTR(estate_related(orig_state), rel) {
		set_true_false_states(SMATCH_EXTRA, rel->name, rel->sym,
				true_state, false_state);
	} END_FOR_EACH_PTR(rel);
}

static void set_extra_chunk_true_false(struct expression *expr,
				       struct smatch_state *true_state,
				       struct smatch_state *false_state)
{
	struct var_sym_list *vsl;
	struct var_sym *vs;
	struct symbol *type;
	char *name;
	struct symbol *sym;

	if (in_warn_on_macro())
		return;

	type = get_type(expr);
	if (!type)
		return;

	name = expr_to_chunk_sym_vsl(expr, &sym, &vsl);
	if (!name || !vsl)
		goto free;
	FOR_EACH_PTR(vsl, vs) {
		store_link(link_id, vs->var, vs->sym, name, sym);
	} END_FOR_EACH_PTR(vs);

	set_true_false_states(SMATCH_EXTRA, name, sym,
			      clone_estate(true_state),
			      clone_estate(false_state));
free:
	free_string(name);
}

static void set_extra_expr_true_false(struct expression *expr,
		struct smatch_state *true_state,
		struct smatch_state *false_state)
{
	char *name;
	struct symbol *sym;
	sval_t sval;

	if (!true_state && !false_state)
		return;

	if (get_value(expr, &sval))
		return;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym) {
		free_string(name);
		set_extra_chunk_true_false(expr, true_state, false_state);
		return;
	}
	set_extra_true_false(name, sym, true_state, false_state);
	free_string(name);
}

static struct sm_state *handle_canonical_while_count_down(struct statement *loop)
{
	struct expression *iter_var;
	struct expression *condition;
	struct sm_state *sm;
	struct smatch_state *estate;
	sval_t start;

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
	if (sval_cmp_val(estate_min(sm->state), 0) < 0)
		return NULL;
	start = estate_max(sm->state);
	if  (sval_cmp_val(start, 0) <= 0)
		return NULL;
	if (!sval_is_max(start))
		start.value--;

	if (condition->type == EXPR_PREOP) {
		estate = alloc_estate_range(sval_type_val(start.type, 1), start);
		if (estate_has_hard_max(sm->state))
			estate_set_hard_max(estate);
		estate_copy_fuzzy_max(estate, sm->state);
		set_extra_expr_mod(iter_var, estate);
	}
	if (condition->type == EXPR_POSTOP) {
		estate = alloc_estate_range(sval_type_val(start.type, 0), start);
		if (estate_has_hard_max(sm->state))
			estate_set_hard_max(estate);
		estate_copy_fuzzy_max(estate, sm->state);
		set_extra_expr_mod(iter_var, estate);
	}
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_inc(struct expression *iter_expr,
						struct expression *condition)
{
	struct expression *iter_var;
	struct sm_state *sm;
	struct smatch_state *estate;
	sval_t start, end, max;

	iter_var = iter_expr->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (!estate_get_single_value(sm->state, &start))
		return NULL;
	if (get_implied_max(condition->right, &end))
		end = sval_cast(get_type(iter_var), end);
	else
		end = sval_type_max(get_type(iter_var));

	if (get_sm_state_expr(SMATCH_EXTRA, condition->left) != sm)
		return NULL;

	switch (condition->op) {
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_NOTEQUAL:
	case '<':
		if (!sval_is_min(end))
			end.value--;
		break;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		break;
	default:
		return NULL;
	}
	if (sval_cmp(end, start) < 0)
		return NULL;
	estate = alloc_estate_range(start, end);
	if (get_hard_max(condition->right, &max)) {
		estate_set_hard_max(estate);
		if (condition->op == '<' ||
		    condition->op == SPECIAL_UNSIGNED_LT ||
		    condition->op == SPECIAL_NOTEQUAL)
			max.value--;
		estate_set_fuzzy_max(estate, max);
	}
	set_extra_expr_mod(iter_var, estate);
	return get_sm_state_expr(SMATCH_EXTRA, iter_var);
}

static struct sm_state *handle_canonical_for_dec(struct expression *iter_expr,
						struct expression *condition)
{
	struct expression *iter_var;
	struct sm_state *sm;
	struct smatch_state *estate;
	sval_t start, end;

	iter_var = iter_expr->unop;
	sm = get_sm_state_expr(SMATCH_EXTRA, iter_var);
	if (!sm)
		return NULL;
	if (!estate_get_single_value(sm->state, &start))
		return NULL;
	if (!get_implied_min(condition->right, &end))
		end = sval_type_min(get_type(iter_var));
	if (get_sm_state_expr(SMATCH_EXTRA, condition->left) != sm)
		return NULL;

	switch (condition->op) {
	case SPECIAL_NOTEQUAL:
	case '>':
		if (!sval_is_min(end) && !sval_is_max(end))
			end.value++;
		break;
	case SPECIAL_GTE:
		break;
	default:
		return NULL;
	}
	if (sval_cmp(end, start) > 0)
		return NULL;
	estate = alloc_estate_range(end, start);
	estate_set_hard_max(estate);
	estate_set_fuzzy_max(estate, estate_get_fuzzy_max(estate));
	set_extra_expr_mod(iter_var, estate);
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

struct sm_state *__extra_handle_canonical_loops(struct statement *loop, struct stree **stree)
{
	struct sm_state *ret;

	__push_fake_cur_stree();
	if (!loop->iterator_post_statement)
		ret = handle_canonical_while_count_down(loop);
	else
		ret = handle_canonical_for_loops(loop);
	*stree = __pop_fake_cur_stree();
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
	sval_t after_value;

	/* paranoid checking.  prolly not needed */
	condition = strip_expr(condition);
	if (!condition)
		return;
	if (condition->type != EXPR_PREOP && condition->type != EXPR_POSTOP)
		return;
	if (condition->op != SPECIAL_DECREMENT)
		return;
	after_value = estate_min(sm->state);
	after_value.value--;
	set_extra_mod(sm->name, sm->sym, alloc_estate_sval(after_value));
}

void __extra_pre_loop_hook_after(struct sm_state *sm,
				struct statement *iterator,
				struct expression *condition)
{
	struct expression *iter_expr;
	sval_t limit;
	struct smatch_state *state;

	if (!iterator) {
		while_count_down_after(sm, condition);
		return;
	}

	iter_expr = iterator->expression;

	if (condition->type != EXPR_COMPARE)
		return;
	if (iter_expr->op == SPECIAL_INCREMENT) {
		limit = sval_binop(estate_max(sm->state), '+',
				   sval_type_val(estate_type(sm->state), 1));
	} else {
		limit = sval_binop(estate_min(sm->state), '-',
				   sval_type_val(estate_type(sm->state), 1));
	}
	if (!estate_has_hard_max(sm->state) && !__has_breaks()) {
		if (iter_expr->op == SPECIAL_INCREMENT)
			state = alloc_estate_range(estate_min(sm->state), limit);
		else
			state = alloc_estate_range(limit, estate_max(sm->state));
	} else {
		state = alloc_estate_sval(limit);
	}
	if (!estate_has_hard_max(sm->state)) {
		estate_clear_hard_max(state);
	}
	if (estate_has_fuzzy_max(sm->state)) {
		sval_t hmax = estate_get_fuzzy_max(sm->state);
		sval_t max = estate_max(sm->state);

		if (sval_cmp(hmax, max) != 0)
			estate_clear_fuzzy_max(state);
	} else if (!estate_has_fuzzy_max(sm->state)) {
		estate_clear_fuzzy_max(state);
	}

	set_extra_mod(sm->name, sm->sym, state);
}

static struct stree *unmatched_stree;
static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;

	if (unmatched_stree) {
		state = get_state_stree(unmatched_stree, SMATCH_EXTRA, sm->name, sm->sym);
		if (state)
			return state;
	}
	if (parent_is_gone_var_sym(sm->name, sm->sym))
		return alloc_estate_empty();
	return alloc_estate_whole(estate_type(sm->state));
}

static void clear_the_pointed_at(struct expression *expr)
{
	struct stree *stree;
	char *name;
	struct symbol *sym;
	struct sm_state *tmp;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	stree = __get_cur_stree();
	FOR_EACH_MY_SM(SMATCH_EXTRA, stree, tmp) {
		if (tmp->name[0] != '*')
			continue;
		if (tmp->sym != sym)
			continue;
		if (strcmp(tmp->name + 1, name) != 0)
			continue;
		set_extra_mod(tmp->name, tmp->sym, alloc_estate_whole(estate_type(tmp->state)));
	} END_FOR_EACH_SM(tmp);

free:
	free_string(name);
}

static void match_function_call(struct expression *expr)
{
	struct expression *arg;
	struct expression *tmp;

	/* if we have the db this is handled in smatch_function_hooks.c */
	if (!option_no_db)
		return;
	if (inlinable(expr->fn))
		return;

	FOR_EACH_PTR(expr->args, arg) {
		tmp = strip_expr(arg);
		if (tmp->type == EXPR_PREOP && tmp->op == '&')
			set_extra_expr_mod(tmp->unop, alloc_estate_whole(get_type(tmp->unop)));
		else
			clear_the_pointed_at(tmp);
	} END_FOR_EACH_PTR(arg);
}

static int values_fit_type(struct expression *left, struct expression *right)
{
	struct range_list *rl;
	struct symbol *type;

	type = get_type(left);
	if (!type)
		return 0;
	get_absolute_rl(right, &rl);
	if (type_unsigned(type) && sval_is_negative(rl_min(rl)))
		return 0;
	if (sval_cmp(sval_type_min(type), rl_min(rl)) > 0)
		return 0;
	if (sval_cmp(sval_type_max(type), rl_max(rl)) < 0)
		return 0;
	return 1;
}

static void save_chunk_info(struct expression *left, struct expression *right)
{
	struct var_sym_list *vsl;
	struct var_sym *vs;
	struct expression *add_expr;
	struct symbol *type;
	sval_t sval;
	char *name;
	struct symbol *sym;

	if (right->type != EXPR_BINOP || right->op != '-')
		return;
	if (!get_value(right->left, &sval))
		return;
	if (!expr_to_sym(right->right))
		return;

	add_expr = binop_expression(left, '+', right->right);
	type = get_type(add_expr);
	if (!type)
		return;
	name = expr_to_chunk_sym_vsl(add_expr, &sym, &vsl);
	if (!name || !vsl)
		goto free;
	FOR_EACH_PTR(vsl, vs) {
		store_link(link_id, vs->var, vs->sym, name, sym);
	} END_FOR_EACH_PTR(vs);

	set_state(SMATCH_EXTRA, name, sym, alloc_estate_sval(sval_cast(type, sval)));
free:
	free_string(name);
}

static void do_array_assign(struct expression *left, int op, struct expression *right)
{
	struct range_list *rl;

	if (op == '=') {
		get_absolute_rl(right, &rl);
		rl = cast_rl(get_type(left), rl);
	} else {
		rl = alloc_whole_rl(get_type(left));
	}

	set_extra_array_mod(left, alloc_estate_rl(rl));
}

static void match_untracked_array(struct expression *call, int param)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(call->args, param);
	arg = strip_expr(arg);

	clear_array_states(arg);
}

static void match_vanilla_assign(struct expression *left, struct expression *right)
{
	struct range_list *orig_rl = NULL;
	struct range_list *rl = NULL;
	struct symbol *right_sym;
	struct symbol *left_type;
	struct symbol *right_type;
	char *right_name = NULL;
	struct symbol *sym;
	char *name;
	sval_t max;
	struct smatch_state *state;
	int comparison;

	if (is_struct(left))
		return;

	save_chunk_info(left, right);

	name = expr_to_var_sym(left, &sym);
	if (!name) {
		if (is_array(left))
			do_array_assign(left, '=', right);
		return;
	}

	left_type = get_type(left);
	right_type = get_type(right);

	right_name = expr_to_var_sym(right, &right_sym);

	if (!__in_fake_assign &&
	    !(right->type == EXPR_PREOP && right->op == '&') &&
	    right_name && right_sym &&
	    values_fit_type(left, right) &&
	    !has_symbol(right, sym)) {
		set_equiv(left, right);
		goto free;
	}

	if (is_pointer(right) && get_address_rl(right, &rl)) {
		state = alloc_estate_rl(rl);
		goto done;
	}

	comparison = get_comparison(left, right);
	if (comparison) {
		comparison = flip_comparison(comparison);
		get_implied_rl(left, &orig_rl);
	}

	if (get_implied_rl(right, &rl)) {
		rl = cast_rl(left_type, rl);
		if (orig_rl)
			filter_by_comparison(&rl, comparison, orig_rl);
		state = alloc_estate_rl(rl);
		if (get_hard_max(right, &max)) {
			estate_set_hard_max(state);
			estate_set_fuzzy_max(state, max);
		}
	} else {
		rl = alloc_whole_rl(right_type);
		rl = cast_rl(left_type, rl);
		if (orig_rl)
			filter_by_comparison(&rl, comparison, orig_rl);
		state = alloc_estate_rl(rl);
	}

done:
	set_extra_mod(name, sym, state);
free:
	free_string(right_name);
}

static int op_remove_assign(int op)
{
	switch (op) {
	case SPECIAL_ADD_ASSIGN:
		return '+';
	case SPECIAL_SUB_ASSIGN:
		return '-';
	case SPECIAL_MUL_ASSIGN:
		return '*';
	case SPECIAL_DIV_ASSIGN:
		return '/';
	case SPECIAL_MOD_ASSIGN:
		return '%';
	case SPECIAL_AND_ASSIGN:
		return '&';
	case SPECIAL_OR_ASSIGN:
		return '|';
	case SPECIAL_XOR_ASSIGN:
		return '^';
	case SPECIAL_SHL_ASSIGN:
		return SPECIAL_LEFTSHIFT;
	case SPECIAL_SHR_ASSIGN:
		return SPECIAL_RIGHTSHIFT;
	default:
		return op;
	}
}

static void match_assign(struct expression *expr)
{
	struct range_list *rl = NULL;
	struct expression *left;
	struct expression *right;
	struct expression *binop_expr;
	struct symbol *left_type;
	struct symbol *right_type;
	struct symbol *sym;
	char *name;
	sval_t left_min, left_max;
	sval_t right_min, right_max;
	sval_t res_min, res_max;

	left = strip_expr(expr->left);

	right = strip_parens(expr->right);
	if (right->type == EXPR_CALL && sym_name_is("__builtin_expect", right->fn))
		right = get_argument_from_call_expr(right->args, 0);
	while (right->type == EXPR_ASSIGNMENT && right->op == '=')
		right = strip_parens(right->left);

	if (expr->op == '=' && is_condition(expr->right))
		return; /* handled in smatch_condition.c */
	if (expr->op == '=' && right->type == EXPR_CALL)
		return; /* handled in smatch_function_hooks.c */
	if (expr->op == '=') {
		match_vanilla_assign(left, right);
		return;
	}

	name = expr_to_var_sym(left, &sym);
	if (!name)
		return;

	left_type = get_type(left);
	right_type = get_type(right);

	res_min = sval_type_min(left_type);
	res_max = sval_type_max(left_type);

	switch (expr->op) {
	case SPECIAL_ADD_ASSIGN:
		get_absolute_max(left, &left_max);
		get_absolute_max(right, &right_max);
		if (sval_binop_overflows(left_max, '+', right_max))
			break;
		if (get_implied_min(left, &left_min) &&
		    !sval_is_negative_min(left_min) &&
		    get_implied_min(right, &right_min) &&
		    !sval_is_negative_min(right_min)) {
			res_min = sval_binop(left_min, '+', right_min);
			res_min = sval_cast(right_type, res_min);
		}
		if (inside_loop())  /* we are assuming loops don't lead to wrapping */
			break;
		res_max = sval_binop(left_max, '+', right_max);
		res_max = sval_cast(right_type, res_max);
		break;
	case SPECIAL_SUB_ASSIGN:
		if (get_implied_max(left, &left_max) &&
		    !sval_is_max(left_max) &&
		    get_implied_min(right, &right_min) &&
		    !sval_is_min(right_min)) {
			res_max = sval_binop(left_max, '-', right_min);
			res_max = sval_cast(right_type, res_max);
		}
		if (inside_loop())
			break;
		if (get_implied_min(left, &left_min) &&
		    !sval_is_min(left_min) &&
		    get_implied_max(right, &right_max) &&
		    !sval_is_max(right_max)) {
			res_min = sval_binop(left_min, '-', right_max);
			res_min = sval_cast(right_type, res_min);
		}
		break;
	case SPECIAL_AND_ASSIGN:
	case SPECIAL_MOD_ASSIGN:
	case SPECIAL_SHL_ASSIGN:
	case SPECIAL_SHR_ASSIGN:
	case SPECIAL_OR_ASSIGN:
	case SPECIAL_XOR_ASSIGN:
	case SPECIAL_MUL_ASSIGN:
	case SPECIAL_DIV_ASSIGN:
		binop_expr = binop_expression(expr->left,
					      op_remove_assign(expr->op),
					      expr->right);
		if (get_absolute_rl(binop_expr, &rl)) {
			rl = cast_rl(left_type, rl);
			set_extra_mod(name, sym, alloc_estate_rl(rl));
			goto free;
		}
		break;
	}
	rl = cast_rl(left_type, alloc_rl(res_min, res_max));
	set_extra_mod(name, sym, alloc_estate_rl(rl));
free:
	free_string(name);
}

static struct smatch_state *increment_state(struct smatch_state *state)
{
	sval_t min = estate_min(state);
	sval_t max = estate_max(state);

	if (!estate_rl(state))
		return NULL;

	if (inside_loop())
		max = sval_type_max(max.type);

	if (!sval_is_min(min) && !sval_is_max(min))
		min.value++;
	if (!sval_is_min(max) && !sval_is_max(max))
		max.value++;
	return alloc_estate_range(min, max);
}

static struct smatch_state *decrement_state(struct smatch_state *state)
{
	sval_t min = estate_min(state);
	sval_t max = estate_max(state);

	if (!estate_rl(state))
		return NULL;

	if (inside_loop())
		min = sval_type_min(min.type);

	if (!sval_is_min(min) && !sval_is_max(min))
		min.value--;
	if (!sval_is_min(max) && !sval_is_max(max))
		max.value--;
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
		if (!state)
			state = alloc_estate_whole(get_type(expr));
		set_extra_expr_mod(expr->unop, state);
		break;
	case SPECIAL_DECREMENT:
		state = get_state_expr(SMATCH_EXTRA, expr->unop);
		state = decrement_state(state);
		if (!state)
			state = alloc_estate_whole(get_type(expr));
		set_extra_expr_mod(expr->unop, state);
		break;
	default:
		return;
	}
}

static void asm_expr(struct statement *stmt)
{

	struct expression *expr;
	struct symbol *type;
	int state = 0;

	FOR_EACH_PTR(stmt->asm_outputs, expr) {
		switch (state) {
		case 0: /* identifier */
		case 1: /* constraint */
			state++;
			continue;
		case 2: /* expression */
			state = 0;
			type = get_type(strip_expr(expr));
			set_extra_expr_mod(expr, alloc_estate_whole(type));
			continue;
		}
	} END_FOR_EACH_PTR(expr);
}

static void check_dereference(struct expression *expr)
{
	if (outside_of_function())
		return;
	set_extra_expr_nomod(expr, alloc_estate_range(valid_ptr_min_sval, valid_ptr_max_sval));
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
	check_dereference(get_array_base(expr));
}

static void set_param_dereferenced(struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	set_extra_nomod(name, sym, alloc_estate_range(valid_ptr_min_sval, valid_ptr_max_sval));

free:
	free_string(name);
}

static sval_t add_one(sval_t sval)
{
	sval.value++;
	return sval;
}

static sval_t sub_one(sval_t sval)
{
	sval.value--;
	return sval;
}

static int handle_postop_inc(struct expression *left, int op, struct expression *right)
{
	struct statement *stmt;
	struct expression *cond;
	struct smatch_state *true_state, *false_state;
	sval_t start;
	sval_t limit;

	/*
	 * If we're decrementing here then that's a canonical while count down
	 * so it's handled already.  We're only handling loops like:
	 * i = 0;
	 * do { ... } while (i++ < 3);
	 */

	if (left->type != EXPR_POSTOP || left->op != SPECIAL_INCREMENT)
		return 0;

	stmt = __cur_stmt->parent;
	if (!stmt)
		return 0;
	if (stmt->type == STMT_COMPOUND)
		stmt = stmt->parent;
	if (!stmt || stmt->type != STMT_ITERATOR || !stmt->iterator_post_condition)
		return 0;

	cond = strip_expr(stmt->iterator_post_condition);
	if (cond->type != EXPR_COMPARE || cond->op != op)
		return 0;
	if (left != strip_expr(cond->left) || right != strip_expr(cond->right))
		return 0;

	if (!get_implied_value(left->unop, &start))
		return 0;
	if (!get_implied_value(right, &limit))
		return 0;

	if (sval_cmp(start, limit) > 0)
		return 0;

	switch (op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		break;
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LTE:
		limit = add_one(limit);
	default:
		return 0;

	}

	true_state = alloc_estate_range(add_one(start), limit);
	false_state = alloc_estate_range(add_one(limit), add_one(limit));

	/* Currently we just discard the false state but when two passes is
	 * implimented correctly then it will use it.
	 */

	set_extra_expr_true_false(left->unop, true_state, false_state);

	return 1;
}

static void handle_comparison(struct symbol *type, struct expression *left, int op, struct expression *right)
{
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
	sval_t min, max, dummy, hard_max;
	int left_postop = 0;
	int right_postop = 0;

	if (left->op == SPECIAL_INCREMENT || left->op == SPECIAL_DECREMENT) {
		if (left->type == EXPR_POSTOP) {
			left->smatch_flags |= Handled;
			left_postop = left->op;
			if (handle_postop_inc(left, op, right))
				return;
		}
		left = strip_parens(left->unop);
	}
	while (left->type == EXPR_ASSIGNMENT)
		left = strip_parens(left->left);

	if (right->op == SPECIAL_INCREMENT || right->op == SPECIAL_DECREMENT) {
		if (right->type == EXPR_POSTOP) {
			right->smatch_flags |= Handled;
			right_postop = right->op;
		}
		right = strip_parens(right->unop);
	}

	/* FIXME: we should be able to use get_real_absolute_rl() here but
	 * apparently that is buggy.
	 */
	get_real_absolute_rl(left, &left_orig);
	left_orig = cast_rl(type, left_orig);

	get_real_absolute_rl(right, &right_orig);
	right_orig = cast_rl(type, right_orig);

	min = sval_type_min(type);
	max = sval_type_max(type);

	left_true = clone_rl(left_orig);
	left_false = clone_rl(left_orig);
	right_true = clone_rl(right_orig);
	right_false = clone_rl(right_orig);

	switch (op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
		left_true = remove_range(left_orig, rl_max(right_orig), max);
		if (!sval_is_min(rl_min(right_orig))) {
			left_false = remove_range(left_orig, min, sub_one(rl_min(right_orig)));
		}

		right_true = remove_range(right_orig, min, rl_min(left_orig));
		if (!sval_is_max(rl_max(left_orig)))
			right_false = remove_range(right_orig, add_one(rl_max(left_orig)), max);
		break;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (!sval_is_max(rl_max(right_orig)))
			left_true = remove_range(left_orig, add_one(rl_max(right_orig)), max);
		left_false = remove_range(left_orig, min, rl_min(right_orig));

		if (!sval_is_min(rl_min(left_orig)))
			right_true = remove_range(right_orig, min, sub_one(rl_min(left_orig)));
		right_false = remove_range(right_orig, rl_max(left_orig), max);

		if (sval_cmp(rl_min(left_orig), rl_min(right_orig)) == 0)
			left_false = remove_range(left_false, rl_min(left_orig), rl_min(left_orig));
		if (sval_cmp(rl_max(left_orig), rl_max(right_orig)) == 0)
			right_false = remove_range(right_false, rl_max(left_orig), rl_max(left_orig));
		break;
	case SPECIAL_EQUAL:
		if (!sval_is_max(rl_max(right_orig))) {
			left_true = remove_range(left_true, add_one(rl_max(right_orig)), max);
		}
		if (!sval_is_min(rl_min(right_orig))) {
			left_true = remove_range(left_true, min, sub_one(rl_min(right_orig)));
		}
		if (sval_cmp(rl_min(right_orig), rl_max(right_orig)) == 0)
			left_false = remove_range(left_orig, rl_min(right_orig), rl_min(right_orig));

		if (!sval_is_max(rl_max(left_orig)))
			right_true = remove_range(right_true, add_one(rl_max(left_orig)), max);
		if (!sval_is_min(rl_min(left_orig)))
			right_true = remove_range(right_true, min, sub_one(rl_min(left_orig)));
		if (sval_cmp(rl_min(left_orig), rl_max(left_orig)) == 0)
			right_false = remove_range(right_orig, rl_min(left_orig), rl_min(left_orig));
		break;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (!sval_is_min(rl_min(right_orig)))
			left_true = remove_range(left_orig, min, sub_one(rl_min(right_orig)));
		left_false = remove_range(left_orig, rl_max(right_orig), max);

		if (!sval_is_max(rl_max(left_orig)))
			right_true = remove_range(right_orig, add_one(rl_max(left_orig)), max);
		right_false = remove_range(right_orig, min, rl_min(left_orig));

		if (sval_cmp(rl_min(left_orig), rl_min(right_orig)) == 0)
			right_false = remove_range(right_false, rl_min(left_orig), rl_min(left_orig));
		if (sval_cmp(rl_max(left_orig), rl_max(right_orig)) == 0)
			left_false = remove_range(left_false, rl_max(left_orig), rl_max(left_orig));
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		left_true = remove_range(left_orig, min, rl_min(right_orig));
		if (!sval_is_max(rl_max(right_orig)))
			left_false = remove_range(left_orig, add_one(rl_max(right_orig)), max);

		right_true = remove_range(right_orig, rl_max(left_orig), max);
		if (!sval_is_min(rl_min(left_orig)))
			right_false = remove_range(right_orig, min, sub_one(rl_min(left_orig)));
		break;
	case SPECIAL_NOTEQUAL:
		if (!sval_is_max(rl_max(right_orig)))
			left_false = remove_range(left_false, add_one(rl_max(right_orig)), max);
		if (!sval_is_min(rl_min(right_orig)))
			left_false = remove_range(left_false, min, sub_one(rl_min(right_orig)));
		if (sval_cmp(rl_min(right_orig), rl_max(right_orig)) == 0)
			left_true = remove_range(left_orig, rl_min(right_orig), rl_min(right_orig));

		if (!sval_is_max(rl_max(left_orig)))
			right_false = remove_range(right_false, add_one(rl_max(left_orig)), max);
		if (!sval_is_min(rl_min(left_orig)))
			right_false = remove_range(right_false, min, sub_one(rl_min(left_orig)));
		if (sval_cmp(rl_min(left_orig), rl_max(left_orig)) == 0)
			right_true = remove_range(right_orig, rl_min(left_orig), rl_min(left_orig));
		break;
	default:
		return;
	}

	left_true = rl_truncate_cast(get_type(strip_expr(left)), left_true);
	left_false = rl_truncate_cast(get_type(strip_expr(left)), left_false);
	right_true = rl_truncate_cast(get_type(strip_expr(right)), right_true);
	right_false = rl_truncate_cast(get_type(strip_expr(right)), right_false);

	left_true_state = alloc_estate_rl(left_true);
	left_false_state = alloc_estate_rl(left_false);
	right_true_state = alloc_estate_rl(right_true);
	right_false_state = alloc_estate_rl(right_false);

	switch (op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (get_hard_max(right, &dummy))
			estate_set_hard_max(left_true_state);
		if (get_hard_max(left, &dummy))
			estate_set_hard_max(right_false_state);
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (get_hard_max(left, &dummy))
			estate_set_hard_max(right_true_state);
		if (get_hard_max(right, &dummy))
			estate_set_hard_max(left_false_state);
		break;
	}

	switch (op) {
	case '<':
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (get_hard_max(right, &hard_max)) {
			if (op == '<' || op == SPECIAL_UNSIGNED_LT)
				hard_max.value--;
			estate_set_fuzzy_max(left_true_state, hard_max);
		}
		if (get_implied_value(right, &hard_max)) {
			if (op == SPECIAL_UNSIGNED_LTE ||
			    op == SPECIAL_LTE)
				hard_max.value++;
			estate_set_fuzzy_max(left_false_state, hard_max);
		}
		if (get_hard_max(left, &hard_max)) {
			if (op == SPECIAL_UNSIGNED_LTE ||
			    op == SPECIAL_LTE)
				hard_max.value--;
			estate_set_fuzzy_max(right_false_state, hard_max);
		}
		if (get_implied_value(left, &hard_max)) {
			if (op == '<' || op == SPECIAL_UNSIGNED_LT)
				hard_max.value++;
			estate_set_fuzzy_max(right_true_state, hard_max);
		}
		break;
	case '>':
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (get_hard_max(left, &hard_max)) {
			if (op == '>' || op == SPECIAL_UNSIGNED_GT)
				hard_max.value--;
			estate_set_fuzzy_max(right_true_state, hard_max);
		}
		if (get_implied_value(left, &hard_max)) {
			if (op == SPECIAL_UNSIGNED_GTE ||
			    op == SPECIAL_GTE)
				hard_max.value++;
			estate_set_fuzzy_max(right_false_state, hard_max);
		}
		if (get_hard_max(right, &hard_max)) {
			if (op == SPECIAL_UNSIGNED_LTE ||
			    op == SPECIAL_LTE)
				hard_max.value--;
			estate_set_fuzzy_max(left_false_state, hard_max);
		}
		if (get_implied_value(right, &hard_max)) {
			if (op == '>' ||
			    op == SPECIAL_UNSIGNED_GT)
				hard_max.value++;
			estate_set_fuzzy_max(left_true_state, hard_max);
		}
		break;
	case SPECIAL_EQUAL:
		if (get_hard_max(left, &hard_max))
			estate_set_fuzzy_max(right_true_state, hard_max);
		if (get_hard_max(right, &hard_max))
			estate_set_fuzzy_max(left_true_state, hard_max);
		break;
	}

	if (get_hard_max(left, &hard_max)) {
		estate_set_hard_max(left_true_state);
		estate_set_hard_max(left_false_state);
	}
	if (get_hard_max(right, &hard_max)) {
		estate_set_hard_max(right_true_state);
		estate_set_hard_max(right_false_state);
	}

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

	if (estate_rl(left_true_state) && estates_equiv(left_true_state, left_false_state)) {
		left_true_state = NULL;
		left_false_state = NULL;
	}

	if (estate_rl(right_true_state) && estates_equiv(right_true_state, right_false_state)) {
		right_true_state = NULL;
		right_false_state = NULL;
	}

	set_extra_expr_true_false(left, left_true_state, left_false_state);
	set_extra_expr_true_false(right, right_true_state, right_false_state);
}

static int is_simple_math(struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->type != EXPR_BINOP)
		return 0;
	switch (expr->op) {
	case '+':
	case '-':
	case '*':
		return 1;
	}
	return 0;
}

static void move_known_values(struct expression **left_p, struct expression **right_p)
{
	struct expression *left = *left_p;
	struct expression *right = *right_p;
	sval_t sval;

	if (get_implied_value(left, &sval)) {
		if (!is_simple_math(right))
			return;
		if (right->op == '*') {
			sval_t divisor;

			if (!get_value(right->right, &divisor))
				return;
			if (divisor.value == 0 && sval.value % divisor.value)
				return;
			*left_p = binop_expression(left, invert_op(right->op), right->right);
			*right_p = right->left;
			return;
		}
		if (right->op == '+' && get_value(right->left, &sval)) {
			*left_p = binop_expression(left, invert_op(right->op), right->left);
			*right_p = right->right;
			return;
		}
		if (get_value(right->right, &sval)) {
			*left_p = binop_expression(left, invert_op(right->op), right->right);
			*right_p = right->left;
			return;
		}
		return;
	}
	if (get_implied_value(right, &sval)) {
		if (!is_simple_math(left))
			return;
		if (left->op == '*') {
			sval_t divisor;

			if (!get_value(left->right, &divisor))
				return;
			if (divisor.value == 0 && sval.value % divisor.value)
				return;
			*right_p = binop_expression(right, invert_op(left->op), left->right);
			*left_p = left->left;
			return;
		}
		if (left->op == '+' && get_value(left->left, &sval)) {
			*right_p = binop_expression(right, invert_op(left->op), left->left);
			*left_p = left->right;
			return;
		}

		if (get_value(left->right, &sval)) {
			*right_p = binop_expression(right, invert_op(left->op), left->right);
			*left_p = left->left;
			return;
		}
		return;
	}
}

static int match_func_comparison(struct expression *expr)
{
	struct expression *left = strip_expr(expr->left);
	struct expression *right = strip_expr(expr->right);
	sval_t sval;

	/*
	 * fixme: think about this harder. We should always be trying to limit
	 * the non-call side as well.  If we can't determine the limitter does
	 * that mean we aren't querying the database and are missing important
	 * information?
	 */

	if (left->type == EXPR_CALL) {
		if (get_implied_value(left, &sval)) {
			handle_comparison(get_type(expr), left, expr->op, right);
			return 1;
		}
		function_comparison(left, expr->op, right);
		return 1;
	}

	if (right->type == EXPR_CALL) {
		if (get_implied_value(right, &sval)) {
			handle_comparison(get_type(expr), left, expr->op, right);
			return 1;
		}
		function_comparison(left, expr->op, right);
		return 1;
	}

	return 0;
}

static void match_comparison(struct expression *expr)
{
	struct expression *left_orig = strip_parens(expr->left);
	struct expression *right_orig = strip_parens(expr->right);
	struct expression *left, *right;
	struct expression *prev;
	struct symbol *type;

	if (match_func_comparison(expr))
		return;

	type = get_type(expr);
	if (!type)
		type = &llong_ctype;

	left = left_orig;
	right = right_orig;
	move_known_values(&left, &right);
	handle_comparison(type, left, expr->op, right);

	prev = get_assigned_expr(left_orig);
	if (is_simple_math(prev) && has_variable(prev, left_orig) == 0) {
		left = prev;
		right = right_orig;
		move_known_values(&left, &right);
		handle_comparison(type, left, expr->op, right);
	}

	prev = get_assigned_expr(right_orig);
	if (is_simple_math(prev) && has_variable(prev, right_orig) == 0) {
		left = left_orig;
		right = prev;
		move_known_values(&left, &right);
		handle_comparison(type, left, expr->op, right);
	}
}

static void handle_AND_condition(struct expression *expr)
{
	struct range_list *rl = NULL;
	sval_t known;

	if (get_implied_value(expr->left, &known) && known.value > 0) {
		known.value--;
		get_absolute_rl(expr->right, &rl);
		rl = remove_range(rl, sval_type_val(known.type, 0), known);
		set_extra_expr_true_false(expr->right, alloc_estate_rl(rl), NULL);
		return;
	}

	if (get_implied_value(expr->right, &known) && known.value > 0) {
		known.value--;
		get_absolute_rl(expr->left, &rl);
		rl = remove_range(rl, sval_type_val(known.type, 0), known);
		set_extra_expr_true_false(expr->left, alloc_estate_rl(rl), NULL);
		return;
	}
}

/* this is actually hooked from smatch_implied.c...  it's hacky, yes */
void __extra_match_condition(struct expression *expr)
{
	struct smatch_state *pre_state;
	struct smatch_state *true_state;
	struct smatch_state *false_state;

	expr = strip_expr(expr);
	switch (expr->type) {
	case EXPR_CALL:
		function_comparison(expr, SPECIAL_NOTEQUAL, zero_expr());
		return;
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF: {
		sval_t zero;

		zero = sval_blank(expr);
		zero.value = 0;

		pre_state = get_extra_state(expr);
		true_state = estate_filter_sval(pre_state, zero);
		if (possibly_true(expr, SPECIAL_EQUAL, zero_expr()))
			false_state = alloc_estate_sval(zero);
		else
			false_state = alloc_estate_empty();
		set_extra_expr_true_false(expr, true_state, false_state);
		return;
	}
	case EXPR_COMPARE:
		match_comparison(expr);
		return;
	case EXPR_ASSIGNMENT:
		__extra_match_condition(expr->left);
		return;
	case EXPR_BINOP:
		if (expr->op == '&')
			handle_AND_condition(expr);
		return;
	}
}

static void assume_indexes_are_valid(struct expression *expr)
{
	struct expression *array_expr;
	int array_size;
	struct expression *offset;
	struct symbol *offset_type;
	struct range_list *rl_before;
	struct range_list *rl_after;
	struct range_list *filter = NULL;
	sval_t size;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	offset = get_array_offset(expr);
	offset_type = get_type(offset);
	if (offset_type && type_signed(offset_type)) {
		filter = alloc_rl(sval_type_min(offset_type),
				  sval_type_val(offset_type, -1));
	}

	array_expr = get_array_base(expr);
	array_size = get_real_array_size(array_expr);
	if (array_size > 1) {
		size = sval_type_val(offset_type, array_size);
		add_range(&filter, size, sval_type_max(offset_type));
	}

	if (!filter)
		return;
	get_absolute_rl(offset, &rl_before);
	rl_after = rl_filter(rl_before, filter);
	if (rl_equiv(rl_before, rl_after))
		return;
	set_extra_expr_nomod(offset, alloc_estate_rl(rl_after));
}

/* returns 1 if it is not possible for expr to be value, otherwise returns 0 */
int implied_not_equal(struct expression *expr, long long val)
{
	return !possibly_false(expr, SPECIAL_NOTEQUAL, value_expr(val));
}

int implied_not_equal_name_sym(char *name, struct symbol *sym, long long val)
{
	struct smatch_state *estate;

	estate = get_state(SMATCH_EXTRA, name, sym);
	if (!estate)
		return 0;
	if (!rl_has_sval(estate_rl(estate), sval_type_val(estate_type(estate), 0)))
		return 1;
	return 0;
}

int parent_is_null_var_sym(const char *name, struct symbol *sym)
{
	char buf[256];
	char *start;
	char *end;
	struct smatch_state *state;

	strncpy(buf, name, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	start = &buf[0];
	while (*start == '*') {
		start++;
		state = get_state(SMATCH_EXTRA, start, sym);
		if (!state)
			continue;
		if (!estate_rl(state))
			return 1;
		if (estate_min(state).value == 0 &&
		    estate_max(state).value == 0)
			return 1;
	}

	start = &buf[0];
	while (*start == '&')
		start++;

	while ((end = strrchr(start, '-'))) {
		*end = '\0';
		state = get_state(SMATCH_EXTRA, start, sym);
		if (!state)
			continue;
		if (estate_min(state).value == 0 &&
		    estate_max(state).value == 0)
			return 1;
	}
	return 0;
}

int parent_is_null(struct expression *expr)
{
	struct symbol *sym;
	char *var;
	int ret = 0;

	expr = strip_expr(expr);
	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto free;
	ret = parent_is_null_var_sym(var, sym);
free:
	free_string(var);
	return ret;
}

static int param_used_callback(void *found, int argc, char **argv, char **azColName)
{
	*(int *)found = 1;
	return 0;
}

static int filter_unused_kzalloc_info(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	sval_t sval;
	int found = 0;

	/* for function pointers assume everything is used */
	if (call->fn->type != EXPR_SYMBOL)
		return 0;

	/*
	 * kzalloc() information is treated as special because so there is just
	 * a lot of stuff initialized to zero and it makes building the database
	 * take hours and hours.
	 *
	 * In theory, we should just remove this line and not pass any unused
	 * information, but I'm not sure enough that this code works so I want
	 * to hold off on that for now.
	 */
	if (!estate_get_single_value(sm->state, &sval) || sval.value != 0)
		return 0;

	run_sql(&param_used_callback, &found,
		"select * from call_implies where %s and type = %d and parameter = %d and key = '%s';",
		get_static_filter(call->fn->symbol), PARAM_USED, param, printed_name);
	if (found)
		return 0;

	/* If the database is not built yet, then assume everything is used */
	run_sql(&param_used_callback, &found,
		"select * from call_implies where %s and type = %d;",
		get_static_filter(call->fn->symbol), PARAM_USED);
	if (!found)
		return 0;

	return 1;
}

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct sm_state *sm)
{
	if (estate_is_whole(sm->state))
		return;
	if (filter_unused_kzalloc_info(call, param, printed_name, sm))
		return;
	sql_insert_caller_info(call, PARAM_VALUE, param, printed_name, sm->state->name);
	if (estate_has_fuzzy_max(sm->state))
		sql_insert_caller_info(call, FUZZY_MAX, param, printed_name,
				       sval_to_str(estate_get_fuzzy_max(sm->state)));
}

static void returned_struct_members(int return_id, char *return_ranges, struct expression *expr)
{
	struct symbol *returned_sym;
	struct sm_state *sm;
	const char *param_name;
	char *compare_str;
	char buf[256];

	returned_sym = expr_to_sym(expr);
	if (!returned_sym)
		return;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (!estate_rl(sm->state))
			continue;
		if (returned_sym != sm->sym)
			continue;

		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		if (strcmp(param_name, "$") == 0)
			continue;
		compare_str = name_sym_to_param_comparison(sm->name, sm->sym);
		if (!compare_str && estate_is_whole(sm->state))
			continue;
		snprintf(buf, sizeof(buf), "%s%s", sm->state->name, compare_str ?: "");

		sql_insert_return_states(return_id, return_ranges, PARAM_VALUE,
					 -1, param_name, buf);
	} END_FOR_EACH_SM(sm);
}

static void db_limited_before(void)
{
	unmatched_stree = clone_stree(__get_cur_stree());
}

static void db_limited_after(void)
{
	free_stree(&unmatched_stree);
}

static void db_param_limit_filter(struct expression *expr, int param, char *key, char *value, enum info_type op)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct sm_state *sm;
	struct symbol *compare_type, *var_type;
	struct range_list *rl;
	struct range_list *limit;
	struct range_list *new;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	if (strcmp(key, "$") == 0)
		compare_type = get_arg_type(expr->fn, param);
	else
		compare_type = get_member_type_from_key(arg, key);

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (sm)
		rl = estate_rl(sm->state);
	else
		rl = alloc_whole_rl(compare_type);

	call_results_to_rl(expr, compare_type, value, &limit);
	new = rl_intersection(rl, limit);

	var_type = get_member_type_from_key(arg, key);
	new = cast_rl(var_type, new);

	/* We want to preserve the implications here */
	if (sm && rl_equiv(estate_rl(sm->state), new))
		__set_sm(sm);
	else {
		if (op == PARAM_LIMIT)
			set_extra_nomod(name, sym, alloc_estate_rl(new));
		else
			set_extra_mod(name, sym, alloc_estate_rl(new));
	}

free:
	free_string(name);
}

static void db_param_limit(struct expression *expr, int param, char *key, char *value)
{
	db_param_limit_filter(expr, param, key, value, PARAM_LIMIT);
}

static void db_param_filter(struct expression *expr, int param, char *key, char *value)
{
	db_param_limit_filter(expr, param, key, value, PARAM_FILTER);
}

static void db_param_add_set(struct expression *expr, int param, char *key, char *value, enum info_type op)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct symbol *type;
	struct smatch_state *state;
	struct range_list *new = NULL;
	struct range_list *added = NULL;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;
	type = get_member_type_from_key(arg, key);
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	state = get_state(SMATCH_EXTRA, name, sym);
	if (state)
		new = estate_rl(state);

	call_results_to_rl(expr, type, value, &added);

	if (op == PARAM_SET)
		new = added;
	else
		new = rl_union(new, added);

	set_extra_mod(name, sym, alloc_estate_rl(new));
free:
	free_string(name);
}

static void db_param_add(struct expression *expr, int param, char *key, char *value)
{
	db_param_add_set(expr, param, key, value, PARAM_ADD);
}

static void db_param_set(struct expression *expr, int param, char *key, char *value)
{
	db_param_add_set(expr, param, key, value, PARAM_SET);
}

static void db_param_value(struct expression *expr, int param, char *key, char *value)
{
	struct expression *call;
	char *name;
	struct symbol *sym;
	struct symbol *type;
	struct range_list *rl = NULL;

	if (param != -1)
		return;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	type = get_member_type_from_key(expr->left, key);
	name = get_variable_from_key(expr->left, key, &sym);
	if (!name || !sym)
		goto free;

	call_results_to_rl(call, type, value, &rl);

	set_extra_mod(name, sym, alloc_estate_rl(rl));
free:
	free_string(name);
}

static void match_call_info(struct expression *expr)
{
	struct smatch_state *state;
	struct range_list *rl = NULL;
	struct expression *arg;
	struct symbol *type;
	int i = 0;

	FOR_EACH_PTR(expr->args, arg) {
		type = get_arg_type(expr->fn, i);

		if (get_implied_rl(arg, &rl))
			rl = cast_rl(type, rl);
		else
			rl = cast_rl(type, alloc_whole_rl(get_type(arg)));

		if (!is_whole_rl(rl))
			sql_insert_caller_info(expr, PARAM_VALUE, i, "$", show_rl(rl));
		state = get_state_expr(SMATCH_EXTRA, arg);
		if (estate_has_fuzzy_max(state)) {
			sql_insert_caller_info(expr, FUZZY_MAX, i, "$",
					       sval_to_str(estate_get_fuzzy_max(state)));
		}
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void set_param_value(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	struct symbol *type;
	char fullname[256];

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 1);
	else
		return;

	type = get_member_type_from_key(symbol_expression(sym), key);
	str_to_rl(type, value, &rl);
	state = alloc_estate_rl(rl);
	set_state(SMATCH_EXTRA, fullname, sym, state);
}

static void set_param_hard_max(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	struct symbol *type;
	char fullname[256];
	sval_t max;

	if (strcmp(key, "*$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$", 1) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 1);
	else
		return;

	state = get_state(SMATCH_EXTRA, fullname, sym);
	if (!state)
		return;
	type = get_member_type_from_key(symbol_expression(sym), key);
	str_to_rl(type, value, &rl);
	if (!rl_to_sval(rl, &max))
		return;
	estate_set_fuzzy_max(state, max);
}

struct smatch_state *get_extra_state(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *ret = NULL;
	struct range_list *rl;

	if (is_pointer(expr) && get_address_rl(expr, &rl))
		return alloc_estate_rl(rl);

	name = expr_to_known_chunk_sym(expr, &sym);
	if (!name)
		goto free;

	ret = get_state(SMATCH_EXTRA, name, sym);
free:
	free_string(name);
	return ret;
}

void register_smatch_extra(int id)
{
	my_id = id;

	add_merge_hook(my_id, &merge_estates);
	add_unmatched_state_hook(my_id, &unmatched_state);
	select_caller_info_hook(set_param_value, PARAM_VALUE);
	select_caller_info_hook(set_param_hard_max, FUZZY_MAX);
	select_return_states_before(&db_limited_before);
	select_return_states_hook(PARAM_LIMIT, &db_param_limit);
	select_return_states_hook(PARAM_FILTER, &db_param_filter);
	select_return_states_hook(PARAM_ADD, &db_param_add);
	select_return_states_hook(PARAM_SET, &db_param_set);
	select_return_states_hook(PARAM_VALUE, &db_param_value);
	select_return_states_after(&db_limited_after);
}

static void match_link_modify(struct sm_state *sm, struct expression *mod_expr)
{
	struct var_sym_list *links;
	struct var_sym *tmp;
	struct smatch_state *state;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		state = get_state(SMATCH_EXTRA, tmp->var, tmp->sym);
		if (!state)
			continue;
		set_state(SMATCH_EXTRA, tmp->var, tmp->sym, alloc_estate_whole(estate_type(state)));
	} END_FOR_EACH_PTR(tmp);
	set_state(link_id, sm->name, sm->sym, &undefined);
}

void register_smatch_extra_links(int id)
{
	link_id = id;
}

void register_smatch_extra_late(int id)
{
	add_merge_hook(link_id, &merge_link_states);
	add_modification_hook(link_id, &match_link_modify);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_call_implies_hook(DEREFERENCE, &set_param_dereferenced);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_assign, GLOBAL_ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&asm_expr, ASM_HOOK);
	add_untracked_param_hook(&match_untracked_array);

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	add_split_return_callback(&returned_struct_members);

	add_hook(&assume_indexes_are_valid, OP_HOOK);
}
