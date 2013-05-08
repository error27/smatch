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

	stmt = get_current_statement();
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

struct sm_state *set_extra_mod(const char *name, struct symbol *sym, struct smatch_state *state)
{
	if (in_warn_on_macro())
		return NULL;
	remove_from_equiv(name, sym);
	call_extra_mod_hooks(name, sym, state);
	return set_state(SMATCH_EXTRA, name, sym, state);
}

struct sm_state *set_extra_expr_mod(struct expression *expr, struct smatch_state *state)
{
	struct symbol *sym;
	char *name;
	struct sm_state *ret = NULL;

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
	struct relation *rel;
	struct smatch_state *orig_state;

	orig_state = get_state(SMATCH_EXTRA, name, sym);

	if (!estate_related(orig_state)) {
		set_state(SMATCH_EXTRA, name, sym, state);
		return;
	}

	set_related(state, estate_related(orig_state));
	FOR_EACH_PTR(estate_related(orig_state), rel) {
		if (option_debug_related)
			sm_msg("updating related %s to %s", rel->name, state->name);
		set_state(SMATCH_EXTRA, rel->name, rel->sym, state);
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
	struct relation *rel;
	struct smatch_state *orig_state;

	if (in_warn_on_macro())
		return;

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

static void set_extra_expr_true_false(struct expression *expr,
		struct smatch_state *true_state,
		struct smatch_state *false_state)
{
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	set_extra_true_false(name, sym, true_state, false_state);
free:
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
		set_extra_expr_mod(iter_var, estate);
	}
	if (condition->type == EXPR_POSTOP) {
		estate = alloc_estate_range(sval_type_val(start.type, 0), start);
		if (estate_has_hard_max(sm->state))
			estate_set_hard_max(estate);
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
	sval_t start, end, dummy;

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
	case SPECIAL_NOTEQUAL:
	case '<':
		if (!sval_is_min(end))
			end.value--;
		break;
	case SPECIAL_LTE:
		break;
	default:
		return NULL;
	}
	if (sval_cmp(end, start) < 0)
		return NULL;
	estate = alloc_estate_range(start, end);
	if (get_hard_max(condition->right, &dummy))
		estate_set_hard_max(estate);
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
	if (!estate_has_hard_max(sm->state))
		estate_clear_hard_max(state);
	set_extra_mod(sm->name, sm->sym, state);
}

static struct state_list *unmatched_slist;
static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;

	if (unmatched_slist) {
		state = get_state_slist(unmatched_slist, SMATCH_EXTRA, sm->name, sm->sym);
		if (state)
			return state;
	}
	return alloc_estate_whole(estate_type(sm->state));
}

static void clear_the_pointed_at(struct expression *expr, struct state_list *slist)
{
	char *name;
	struct symbol *sym;
	struct sm_state *tmp;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	FOR_EACH_PTR(slist, tmp) {
		if (tmp->name[0] != '*')
			continue;
		if (tmp->sym != sym)
			continue;
		if (strcmp(tmp->name + 1, name) != 0)
			continue;
		set_extra_mod(tmp->name, tmp->sym, alloc_estate_whole(estate_type(tmp->state)));
	} END_FOR_EACH_PTR(tmp);

free:
	free_string(name);
}

static void match_function_call(struct expression *expr)
{
	struct expression *arg;
	struct expression *tmp;
	struct state_list *slist;

	/* if we have the db this is handled in smatch_function_hooks.c */
	if (!option_no_db)
		return;
	if (inlinable(expr->fn))
		return;

	slist = get_all_states(SMATCH_EXTRA);

	FOR_EACH_PTR(expr->args, arg) {
		tmp = strip_expr(arg);
		if (tmp->type == EXPR_PREOP && tmp->op == '&')
			set_extra_expr_mod(tmp->unop, alloc_estate_whole(get_type(tmp->unop)));
		else
			clear_the_pointed_at(tmp, slist);
	} END_FOR_EACH_PTR(arg);

	free_slist(&slist);
}

static int types_equiv_or_pointer(struct symbol *one, struct symbol *two)
{
	if (!one || !two)
		return 0;
	if (one->type == SYM_PTR && two->type == SYM_PTR)
		return 1;
	return types_equiv(one, two);
}

static void match_vanilla_assign(struct expression *left, struct expression *right)
{
	struct range_list *rl = NULL;
	struct symbol *right_sym;
	struct symbol *left_type;
	struct symbol *right_type;
	char *right_name = NULL;
	struct symbol *sym;
	char *name;
	sval_t tmp;
	struct smatch_state *state;

	name = expr_to_var_sym(left, &sym);
	if (!name)
		return;

	left_type = get_type(left);
	right_type = get_type(right);

	right_name = expr_to_var_sym(right, &right_sym);
	if (right_name && right_sym &&
	    types_equiv_or_pointer(left_type, right_type)) {
		set_equiv(left, right);
		goto free;
	}

	if (get_implied_rl(right, &rl)) {
		rl = cast_rl(left_type, rl);
		state = alloc_estate_rl(rl);
		if (get_hard_max(right, &tmp))
			estate_set_hard_max(state);
	} else {
		rl = alloc_whole_rl(right_type);
		rl = cast_rl(left_type, rl);
		state = alloc_estate_rl(rl);
	}
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

	right = strip_expr(expr->right);
	while (right->type == EXPR_ASSIGNMENT && right->op == '=')
		right = strip_expr(right->left);

	if (is_condition(expr->right))
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

	res_min = sval_type_min(right_type);
	res_max = sval_type_max(right_type);

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
		binop_expr = binop_expression(expr->left,
					      op_remove_assign(expr->op),
					      expr->right);
		if (get_implied_rl(binop_expr, &rl)) {
			rl = cast_rl(left_type, rl);
			set_extra_mod(name, sym, alloc_estate_rl(rl));
			goto free;
		}
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
			set_state(SMATCH_EXTRA, name, sym, alloc_estate_whole(get_real_base_type(sym)));
			scoped_state_extra(name, sym);
		}
	}
}

static void check_dereference(struct expression *expr)
{
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
		set_state(my_id, arg->ident->name, arg, alloc_estate_whole(get_real_base_type(arg)));
	} END_FOR_EACH_PTR(arg);
}

static int match_func_comparison(struct expression *expr)
{
	struct expression *left = strip_expr(expr->left);
	struct expression *right = strip_expr(expr->right);
	sval_t sval;

	if (left->type == EXPR_CALL) {
		if (!get_implied_value(right, &sval))
			return 1;
		function_comparison(expr->op, left, sval, 1);
		return 1;
	}

	if (right->type == EXPR_CALL) {
		if (!get_implied_value(left, &sval))
			return 1;
		function_comparison(expr->op, right, sval, 0);
		return 1;
	}

	return 0;
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
	sval_t min, max, dummy;
	int left_postop = 0;
	int right_postop = 0;

	if (left->op == SPECIAL_INCREMENT || left->op == SPECIAL_DECREMENT) {
		if (left->type == EXPR_POSTOP) {
			left->smatch_flags |= Handled;
			left_postop = left->op;
		}
		left = strip_expr(left->unop);
	}
	while (left->type == EXPR_ASSIGNMENT)
		left = strip_expr(left->left);

	if (right->op == SPECIAL_INCREMENT || right->op == SPECIAL_DECREMENT) {
		if (right->type == EXPR_POSTOP) {
			right->smatch_flags |= Handled;
			right_postop = right->op;
		}
		right = strip_expr(right->unop);
	}

	if (get_implied_rl(left, &left_orig)) {
		left_orig = cast_rl(type, left_orig);
	} else {
		min = sval_type_min(get_type(left));
		max = sval_type_max(get_type(left));
		left_orig = cast_rl(type, alloc_rl(min, max));
	}

	if (get_implied_rl(right, &right_orig)) {
		right_orig = cast_rl(type, right_orig);
	} else {
		min = sval_type_min(get_type(right));
		max = sval_type_max(get_type(right));
		right_orig = cast_rl(type, alloc_rl(min, max));
	}
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

	left_true = rl_truncate_cast(get_type(left), left_true);
	left_false = rl_truncate_cast(get_type(left), left_false);
	right_true = rl_truncate_cast(get_type(right), right_true);
	right_false = rl_truncate_cast(get_type(right), right_false);

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

	if (get_hard_max(left, &dummy)) {
		estate_set_hard_max(left_true_state);
		estate_set_hard_max(left_false_state);
	}
	if (get_hard_max(right, &dummy)) {
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

	set_extra_expr_true_false(left, left_true_state, left_false_state);
	set_extra_expr_true_false(right, right_true_state, right_false_state);
}

static void match_comparison(struct expression *expr)
{
	struct expression *left = strip_expr(expr->left);
	struct expression *right = strip_expr(expr->right);
	struct symbol *type;

	if (match_func_comparison(expr))
		return;

	type = get_type(expr);
	if (!type)
		type = &llong_ctype;

	handle_comparison(type, left, expr->op, right);
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
		function_comparison(SPECIAL_NOTEQUAL, expr, ll_to_sval(0), 1);
		return;
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF: {
		sval_t zero;

		zero = sval_blank(expr);
		zero.value = 0;

		name = expr_to_var_sym(expr, &sym);
		if (!name)
			return;
		pre_state = get_state(my_id, name, sym);
		true_state = estate_filter_sval(pre_state, zero);
		if (possibly_true(expr, SPECIAL_EQUAL, zero_expr()))
			false_state = alloc_estate_sval(zero);
		else
			false_state = alloc_estate_empty();
		set_extra_true_false(name, sym, true_state, false_state);
		free_string(name);
		return;
	}
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

static void struct_member_callback(struct expression *call, int param, char *printed_name, struct smatch_state *state)
{
	if (estate_is_whole(state))
		return;
	sql_insert_caller_info(call, PARAM_VALUE, param, printed_name, state->name);
}

static void db_limited_before(void)
{
	unmatched_slist = clone_slist(__get_cur_slist());
}

static void db_limited_after(void)
{
	free_slist(&unmatched_slist);
}

static void db_param_limit_filter(struct expression *expr, int param, char *key, char *value, int mod)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct sm_state *sm;
	struct symbol *type;
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

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	type = get_member_type_from_key(arg, key);
	if (sm)
		rl = estate_rl(sm->state);
	else
		rl = alloc_whole_rl(type);

	str_to_rl(type, value, &limit);
	new = rl_intersection(rl, limit);

	/* We want to preserve the implications here */
	if (sm && rl_equiv(estate_rl(sm->state), new)) {
		__set_sm(sm);
	} else {
		if (mod)
			set_extra_mod(name, sym, alloc_estate_rl(new));
		else
			set_extra_nomod(name, sym, alloc_estate_rl(new));
	}

free:
	free_string(name);
}

static void db_param_limit(struct expression *expr, int param, char *key, char *value)
{
	db_param_limit_filter(expr, param, key, value, 0);
}

static void db_param_filter(struct expression *expr, int param, char *key, char *value)
{
	db_param_limit_filter(expr, param, key, value, 1);
}

static void db_param_add(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct symbol *type;
	struct smatch_state *state;
	struct range_list *added = NULL;
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

	type = get_member_type_from_key(arg, key);
	state = get_state(SMATCH_EXTRA, name, sym);
	if (state) {
		str_to_rl(type, value, &added);
		new = rl_union(estate_rl(state), added);
	} else {
		new = alloc_whole_rl(type);
	}

	set_extra_mod(name, sym, alloc_estate_rl(new));
free:
	free_string(name);
}

static void db_returned_member_info(struct expression *expr, int param, char *key, char *value)
{
	struct symbol *type;
	struct symbol *sym;
	char *name;
	char member_name[256];
	struct range_list *rl;

	if (expr->type != EXPR_ASSIGNMENT)
		return;

	name = expr_to_var_sym(expr->left, &sym);
	if (!name || !sym)
		goto free;
	snprintf(member_name, sizeof(member_name), "%s%s", name, key + 2);
	type = get_member_type_from_key(expr->left, key);
	if (!type)
		return;
	str_to_rl(type, value, &rl);
	set_extra_mod(member_name, sym, alloc_estate_rl(rl));

free:
	free_string(name);
}

static void returned_member_callback(int return_id, char *return_ranges, char *printed_name, struct smatch_state *state)
{
	if (estate_is_whole(state))
		return;

	sql_insert_return_states(return_id, return_ranges, RETURN_VALUE, -1,
			printed_name, state->name);
}

static void match_call_info(struct expression *expr)
{
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

		sql_insert_caller_info(expr, PARAM_VALUE, i, "$$", show_rl(rl));
		i++;
	} END_FOR_EACH_PTR(arg);
}

static void set_param_value(const char *name, struct symbol *sym, char *key, char *value)
{
	struct range_list *rl = NULL;
	struct smatch_state *state;
	struct symbol *type;
	char fullname[256];

	if (strcmp(key, "*$$") == 0)
		snprintf(fullname, sizeof(fullname), "*%s", name);
	else if (strncmp(key, "$$", 2) == 0)
		snprintf(fullname, 256, "%s%s", name, key + 2);
	else
		return;

	type = get_member_type_from_key(symbol_expression(sym), key);
	str_to_rl(type, value, &rl);
	state = alloc_estate_rl(rl);
	set_state(SMATCH_EXTRA, fullname, sym, state);
}

void register_smatch_extra(int id)
{
	my_id = id;

	add_merge_hook(my_id, &merge_estates);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_definition_db_callback(set_param_value, PARAM_VALUE);
	add_db_return_states_callback(RETURN_VALUE, &db_returned_member_info);
	add_db_return_states_before(&db_limited_before);
	add_db_return_states_callback(LIMITED_VALUE, &db_param_limit);
	add_db_return_states_callback(FILTER_VALUE, &db_param_filter);
	add_db_return_states_callback(ADDED_VALUE, &db_param_add);
	add_db_return_states_after(&db_limited_after);
}

void register_smatch_extra_late(int id)
{
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	add_db_fn_call_callback(DEREFERENCE, &set_param_dereferenced);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&asm_expr, ASM_HOOK);

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
	add_member_info_callback(my_id, struct_member_callback);
	add_returned_member_callback(my_id, returned_member_callback);
}
