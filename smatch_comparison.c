/*
 * smatch/smatch_comparison.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The point here is to store the relationships between two variables.
 * Ie:  y > x.
 * To do that we create a state with the two variables in alphabetical order:
 * ->name = "x vs y" and the state would be "<".  On the false path the state
 * would be ">=".
 *
 * Part of the trick of it is that if x or y is modified then we need to reset
 * the state.  We need to keep a list of all the states which depend on x and
 * all the states which depend on y.  The link_id code handles this.
 *
 * Future work:  If we know that x is greater than y and y is greater than z
 * then we know that x is greater than z.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int compare_id;
static int link_id;

static struct smatch_state compare_states[] = {
	['<'] = {
		.name = "<",
		.data = (void *)'<',
	},
	[SPECIAL_UNSIGNED_LT] = {
		.name = "<",
		.data = (void *)SPECIAL_UNSIGNED_LT,
	},
	[SPECIAL_LTE] = {
		.name = "<=",
		.data = (void *)SPECIAL_LTE,
	},
	[SPECIAL_UNSIGNED_LTE] = {
		.name = "<=",
		.data = (void *)SPECIAL_UNSIGNED_LTE,
	},
	[SPECIAL_EQUAL] = {
		.name = "==",
		.data = (void *)SPECIAL_EQUAL,
	},
	[SPECIAL_NOTEQUAL] = {
		.name = "!=",
		.data = (void *)SPECIAL_NOTEQUAL,
	},
	[SPECIAL_GTE] = {
		.name = ">=",
		.data = (void *)SPECIAL_GTE,
	},
	[SPECIAL_UNSIGNED_GTE] = {
		.name = ">=",
		.data = (void *)SPECIAL_UNSIGNED_GTE,
	},
	['>'] = {
		.name = ">",
		.data = (void *)'>',
	},
	[SPECIAL_UNSIGNED_GT] = {
		.name = ">",
		.data = (void *)SPECIAL_UNSIGNED_GT,
	},
};

static int flip_op(int op)
{
	switch (op) {
	case 0:
		return 0;
	case '<':
		return '>';
	case SPECIAL_UNSIGNED_LT:
		return SPECIAL_UNSIGNED_GT;
	case SPECIAL_LTE:
		return SPECIAL_GTE;
	case SPECIAL_UNSIGNED_LTE:
		return SPECIAL_UNSIGNED_GTE;
	case SPECIAL_EQUAL:
		return SPECIAL_EQUAL;
	case SPECIAL_NOTEQUAL:
		return SPECIAL_NOTEQUAL;
	case SPECIAL_GTE:
		return SPECIAL_LTE;
	case SPECIAL_UNSIGNED_GTE:
		return SPECIAL_UNSIGNED_LTE;
	case '>':
		return '<';
	case SPECIAL_UNSIGNED_GT:
		return SPECIAL_UNSIGNED_LT;
	default:
		sm_msg("internal smatch bug.  unhandled comparison %d", op);
		return op;
	}
}

static int falsify_op(int op)
{
	switch (op) {
	case 0:
		return 0;
	case '<':
		return SPECIAL_GTE;
	case SPECIAL_UNSIGNED_LT:
		return SPECIAL_UNSIGNED_GTE;
	case SPECIAL_LTE:
		return '>';
	case SPECIAL_UNSIGNED_LTE:
		return SPECIAL_UNSIGNED_GT;
	case SPECIAL_EQUAL:
		return SPECIAL_NOTEQUAL;
	case SPECIAL_NOTEQUAL:
		return SPECIAL_EQUAL;
	case SPECIAL_GTE:
		return '<';
	case SPECIAL_UNSIGNED_GTE:
		return SPECIAL_UNSIGNED_LT;
	case '>':
		return SPECIAL_LTE;
	case SPECIAL_UNSIGNED_GT:
		return SPECIAL_UNSIGNED_LTE;
	default:
		sm_msg("internal smatch bug.  unhandled comparison %d", op);
		return op;
	}
}

struct smatch_state *alloc_link_state(struct string_list *links)
{
	struct smatch_state *state;
	static char buf[256];
	char *tmp;
	int i;

	state = __alloc_smatch_state(0);

	i = 0;
	FOR_EACH_PTR(links, tmp) {
		if (!i++)
			snprintf(buf, sizeof(buf), "%s", tmp);
		else
			snprintf(buf, sizeof(buf), "%s, %s", buf, tmp);
	} END_FOR_EACH_PTR(tmp);

	state->name = alloc_sname(buf);
	state->data = links;
	return state;
}

static void save_start_states(struct statement *stmt)
{
	struct symbol *param;
	char orig[64];
	char state_name[128];
	struct smatch_state *state;
	struct string_list *links;
	char *link;

	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, param) {
		if (!param->ident)
			continue;
		snprintf(orig, sizeof(orig), "%s orig", param->ident->name);
		snprintf(state_name, sizeof(state_name), "%s vs %s", param->ident->name, orig);
		state = &compare_states[SPECIAL_EQUAL];
		set_state(compare_id, state_name, NULL, state);

		link = alloc_sname(state_name);
		links = NULL;
		insert_string(&links,  link);
		state = alloc_link_state(links);
		set_state(link_id, param->ident->name, param, state);
	} END_FOR_EACH_PTR(param);
}

static struct smatch_state *merge_func(struct smatch_state *s1, struct smatch_state *s2)
{
	struct smatch_state *ret;
	struct string_list *links;

	links = combine_string_lists(s1->data, s2->data);
	ret = alloc_link_state(links);
	return ret;
}

static void save_link(struct expression *expr, char *link)
{
	struct smatch_state *old_state, *new_state;
	struct string_list *links;
	char *new;

	old_state = get_state_expr(link_id, expr);
	if (old_state)
		links = clone_str_list(old_state->data);
	else
		links = NULL;

	new = alloc_sname(link);
	insert_string(&links, new);

	new_state = alloc_link_state(links);
	set_state_expr(link_id, expr, new_state);
}

static void match_inc(struct sm_state *sm)
{
	struct string_list *links;
	struct smatch_state *state;
	char *tmp;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		state = get_state(compare_id, tmp, NULL);
		if (state == &compare_states[SPECIAL_EQUAL] ||
		    state == &compare_states[SPECIAL_GTE] ||
		    state == &compare_states[SPECIAL_UNSIGNED_GTE] ||
		    state == &compare_states['>'] ||
		    state == &compare_states[SPECIAL_UNSIGNED_GT]) {
			set_state(compare_id, tmp, NULL, &compare_states['>']);
		} else {
			set_state(compare_id, tmp, NULL, &undefined);
		}
	} END_FOR_EACH_PTR(tmp);
}

static void match_dec(struct sm_state *sm)
{
	struct string_list *links;
	struct smatch_state *state;
	char *tmp;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		state = get_state(compare_id, tmp, NULL);
		if (state == &compare_states[SPECIAL_EQUAL] ||
		    state == &compare_states[SPECIAL_LTE] ||
		    state == &compare_states[SPECIAL_UNSIGNED_LTE] ||
		    state == &compare_states['<'] ||
		    state == &compare_states[SPECIAL_UNSIGNED_LT]) {
			set_state(compare_id, tmp, NULL, &compare_states['<']);
		} else {
			set_state(compare_id, tmp, NULL, &undefined);
		}
	} END_FOR_EACH_PTR(tmp);
}

static int match_inc_dec(struct sm_state *sm, struct expression *mod_expr)
{
	if (!mod_expr)
		return 0;
	if (mod_expr->type != EXPR_PREOP && mod_expr->type != EXPR_POSTOP)
		return 0;

	if (mod_expr->op == SPECIAL_INCREMENT) {
		match_inc(sm);
		return 1;
	}
	if (mod_expr->op == SPECIAL_DECREMENT) {
		match_dec(sm);
		return 1;
	}
	return 0;
}

static void match_modify(struct sm_state *sm, struct expression *mod_expr)
{
	struct string_list *links;
	char *tmp;

	if (match_inc_dec(sm, mod_expr))
		return;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		set_state(compare_id, tmp, NULL, &undefined);
	} END_FOR_EACH_PTR(tmp);
	set_state(link_id, sm->name, sm->sym, &undefined);
}

static void match_logic(struct expression *expr)
{
	char *left = NULL;
	char *right = NULL;
	struct symbol *left_sym, *right_sym;
	int op, false_op;
	struct smatch_state *true_state, *false_state;
	char state_name[256];

	if (expr->type != EXPR_COMPARE)
		return;
	left = expr_to_var_sym(expr->left, &left_sym);
	if (!left || !left_sym)
		goto free;
	right = expr_to_var_sym(expr->right, &right_sym);
	if (!right || !right_sym)
		goto free;

	if (strcmp(left, right) > 0) {
		char *tmp = left;
		left = right;
		right = tmp;
		op = flip_op(expr->op);
	} else {
		op = expr->op;
	}
	false_op = falsify_op(op);
	snprintf(state_name, sizeof(state_name), "%s vs %s", left, right);
	true_state = &compare_states[op];
	false_state = &compare_states[false_op];

	set_true_false_states(compare_id, state_name, NULL, true_state, false_state);
	save_link(expr->left, state_name);
	save_link(expr->right, state_name);
free:
	free_string(left);
	free_string(right);
}

static void add_comparison(struct expression *left, int comparison, struct expression *right)
{
	char *left_name = NULL;
	char *right_name = NULL;
	struct symbol *left_sym, *right_sym;
	struct smatch_state *state;
	char state_name[256];

	left_name = expr_to_var_sym(left, &left_sym);
	if (!left_name || !left_sym)
		goto free;
	right_name = expr_to_var_sym(right, &right_sym);
	if (!right_name || !right_sym)
		goto free;

	if (strcmp(left_name, right_name) > 0) {
		char *tmp = left_name;
		left_name = right_name;
		right_name = tmp;
		comparison = flip_op(comparison);
	}
	snprintf(state_name, sizeof(state_name), "%s vs %s", left_name, right_name);
	state = &compare_states[comparison];

	set_state(compare_id, state_name, NULL, state);
	save_link(left, state_name);
	save_link(right, state_name);
free:
	free_string(left_name);
	free_string(right_name);

}

static void match_assign_add(struct expression *expr)
{
	struct expression *right;
	struct expression *r_left, *r_right;
	sval_t left_tmp, right_tmp;

	right = strip_expr(expr->right);
	r_left = strip_expr(right->left);
	r_right = strip_expr(right->right);

	if (!is_capped(expr->left)) {
		get_absolute_max(r_left, &left_tmp);
		get_absolute_max(r_right, &right_tmp);
		if (sval_binop_overflows(left_tmp, '+', right_tmp))
			return;
	}

	get_absolute_min(r_left, &left_tmp);
	if (sval_is_negative(left_tmp))
		return;
	get_absolute_min(r_right, &right_tmp);
	if (sval_is_negative(right_tmp))
		return;

	if (left_tmp.value == 0)
		add_comparison(expr->left, SPECIAL_GTE, r_right);
	else
		add_comparison(expr->left, '>', r_right);

	if (right_tmp.value == 0)
		add_comparison(expr->left, SPECIAL_GTE, r_left);
	else
		add_comparison(expr->left, '>', r_left);
}

static void match_assign_sub(struct expression *expr)
{
	struct expression *right;
	struct expression *r_left, *r_right;
	int comparison;
	sval_t min;

	right = strip_expr(expr->right);
	r_left = strip_expr(right->left);
	r_right = strip_expr(right->right);

	if (get_absolute_min(r_right, &min) && sval_is_negative(min))
		return;

	comparison = get_comparison(r_left, r_right);

	switch (comparison) {
	case '>':
	case SPECIAL_GTE:
		if (implied_not_equal(r_right, 0))
			add_comparison(expr->left, '>', r_left);
		else
			add_comparison(expr->left, SPECIAL_GTE, r_left);
		return;
	}
}

static void match_binop_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->op == '+')
		match_assign_add(expr);
	if (right->op == '-')
		match_assign_sub(expr);
}

static void match_normal_assign(struct expression *expr)
{
	add_comparison(expr->left, SPECIAL_EQUAL, expr->right);
}

static void match_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->type == EXPR_BINOP)
		match_binop_assign(expr);
	else
		match_normal_assign(expr);
}

static int get_comparison_strings(char *one, char *two)
{
	char buf[256];
	struct smatch_state *state;
	int invert = 0;
	int ret = 0;

	if (strcmp(one, two) > 0) {
		char *tmp = one;

		one = two;
		two = tmp;
		invert = 1;
	}

	snprintf(buf, sizeof(buf), "%s vs %s", one, two);
	state = get_state(compare_id, buf, NULL);
	if (state)
		ret = PTR_INT(state->data);

	if (invert)
		ret = flip_op(ret);

	return ret;
}

int get_comparison(struct expression *a, struct expression *b)
{
	char *one = NULL;
	char *two = NULL;
	int ret = 0;

	one = expr_to_var(a);
	if (!one)
		goto free;
	two = expr_to_var(b);
	if (!two)
		goto free;

	ret = get_comparison_strings(one, two);
free:
	free_string(one);
	free_string(two);
	return ret;
}

void __add_comparison_info(struct expression *expr, struct expression *call, const char *range)
{
	struct expression *arg;
	int comparison;
	const char *c = range;

	if (!str_to_comparison_arg(c, call, &comparison, &arg, NULL))
		return;
	add_comparison(expr, SPECIAL_LTE, arg);
}

char *range_comparison_to_param(struct expression *expr)
{
	struct symbol *param;
	char *var = NULL;
	char buf[256];
	char *ret_str = NULL;
	int compare;
	sval_t min;
	int i;

	var = expr_to_var(expr);
	if (!var)
		goto free;
	get_absolute_min(expr, &min);

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, param) {
		i++;
		if (!param->ident)
			continue;
		snprintf(buf, sizeof(buf), "%s orig", param->ident->name);
		compare = get_comparison_strings(var, buf);
		if (!compare)
			continue;
		if (compare == SPECIAL_EQUAL) {
			snprintf(buf, sizeof(buf), "[%sp%d]", show_special(compare), i);
			ret_str = alloc_sname(buf);
		} else if (show_special(compare)[0] == '<') {
			snprintf(buf, sizeof(buf), "%s-[%sp%d]", sval_to_str(min),
				 show_special(compare), i);
			ret_str = alloc_sname(buf);

		}
	} END_FOR_EACH_PTR(param);

free:
	free_string(var);
	return ret_str;
}

void register_comparison(int id)
{
	compare_id = id;
	add_hook(&match_logic, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&save_start_states, AFTER_DEF_HOOK);
}

void register_comparison_links(int id)
{
	link_id = id;
	add_merge_hook(link_id, &merge_func);
	add_modification_hook(link_id, &match_modify);
}
