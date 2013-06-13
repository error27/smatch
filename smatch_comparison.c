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

struct compare_data {
	const char *var1;
	struct symbol *sym1;
	const char *var2;
	struct symbol *sym2;
	int comparison;
};
ALLOCATOR(compare_data, "compare data");

static struct smatch_state *alloc_compare_state(
		const char *var1, struct symbol *sym1,
		const char *var2, struct symbol *sym2,
		int comparison)
{
	struct smatch_state *state;
	struct compare_data *data;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(show_special(comparison));
	data = __alloc_compare_data(0);
	data->var1 = alloc_sname(var1);
	data->sym1 = sym1;
	data->var2 = alloc_sname(var2);
	data->sym2 = sym2;
	data->comparison = comparison;
	state->data = data;
	return state;
}

static int state_to_comparison(struct smatch_state *state)
{
	if (!state || !state->data)
		return 0;
	return ((struct compare_data *)state->data)->comparison;
}

/*
 * flip_op() reverses the op left and right.  So "x >= y" becomes "y <= x".
 */
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

static int rl_comparison(struct range_list *left_rl, struct range_list *right_rl)
{
	sval_t left_min, left_max, right_min, right_max;

	if (!left_rl || !right_rl)
		return 0;

	left_min = rl_min(left_rl);
	left_max = rl_max(left_rl);
	right_min = rl_min(right_rl);
	right_max = rl_max(right_rl);

	if (left_min.value == left_max.value &&
	    right_min.value == right_max.value &&
	    left_min.value == right_min.value)
		return SPECIAL_EQUAL;

	if (sval_cmp(left_max, right_min) < 0)
		return '<';
	if (sval_cmp(left_max, right_min) == 0)
		return SPECIAL_LTE;
	if (sval_cmp(left_min, right_max) > 0)
		return '>';
	if (sval_cmp(left_min, right_max) == 0)
		return SPECIAL_GTE;

	return 0;
}

static struct smatch_state *unmatched_comparison(struct sm_state *sm)
{
	struct compare_data *data = sm->state->data;
	struct range_list *left_rl, *right_rl;
	int op;

	if (!data)
		return &undefined;

	if (!get_implied_rl_var_sym(data->var1, data->sym1, &left_rl))
		return &undefined;
	if (!get_implied_rl_var_sym(data->var2, data->sym2, &right_rl))
		return &undefined;

	op = rl_comparison(left_rl, right_rl);
	if (op)
		return alloc_compare_state(data->var1, data->sym1, data->var2, data->sym2, op);

	return &undefined;
}

/* remove_unsigned_from_comparison() is obviously a hack. */
static int remove_unsigned_from_comparison(int op)
{
	switch (op) {
	case SPECIAL_UNSIGNED_LT:
		return '<';
	case SPECIAL_UNSIGNED_LTE:
		return SPECIAL_LTE;
	case SPECIAL_UNSIGNED_GTE:
		return SPECIAL_GTE;
	case SPECIAL_UNSIGNED_GT:
		return '>';
	default:
		return op;
	}
}

static int merge_comparisons(int one, int two)
{
	int LT, EQ, GT;

	one = remove_unsigned_from_comparison(one);
	two = remove_unsigned_from_comparison(two);

	LT = EQ = GT = 0;

	switch (one) {
	case '<':
		LT = 1;
		break;
	case SPECIAL_LTE:
		LT = 1;
		EQ = 1;
		break;
	case SPECIAL_EQUAL:
		EQ = 1;
		break;
	case SPECIAL_GTE:
		GT = 1;
		EQ = 1;
		break;
	case '>':
		GT = 1;
	}

	switch (two) {
	case '<':
		LT = 1;
		break;
	case SPECIAL_LTE:
		LT = 1;
		EQ = 1;
		break;
	case SPECIAL_EQUAL:
		EQ = 1;
		break;
	case SPECIAL_GTE:
		GT = 1;
		EQ = 1;
		break;
	case '>':
		GT = 1;
	}

	if (LT && EQ && GT)
		return 0;
	if (LT && EQ)
		return SPECIAL_LTE;
	if (LT && GT)
		return SPECIAL_NOTEQUAL;
	if (LT)
		return '<';
	if (EQ && GT)
		return SPECIAL_GTE;
	if (GT)
		return '>';
	return 0;
}

static struct smatch_state *merge_compare_states(struct smatch_state *s1, struct smatch_state *s2)
{
	struct compare_data *data = s1->data;
	int op;

	op = merge_comparisons(state_to_comparison(s1), state_to_comparison(s2));
	if (op)
		return alloc_compare_state(data->var1, data->sym1, data->var2, data->sym2, op);
	return &undefined;
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
		state = alloc_compare_state(param->ident->name, param, alloc_sname(orig), NULL, SPECIAL_EQUAL);
		set_state(compare_id, state_name, NULL, state);

		link = alloc_sname(state_name);
		links = NULL;
		insert_string(&links,  link);
		state = alloc_link_state(links);
		set_state(link_id, param->ident->name, param, state);
	} END_FOR_EACH_PTR(param);
}

static struct smatch_state *merge_links(struct smatch_state *s1, struct smatch_state *s2)
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

		switch (state_to_comparison(state)) {
		case SPECIAL_EQUAL:
		case SPECIAL_GTE:
		case SPECIAL_UNSIGNED_GTE:
		case '>':
		case SPECIAL_UNSIGNED_GT: {
			struct compare_data *data = state->data;
			struct smatch_state *new;

			new = alloc_compare_state(data->var1, data->sym1, data->var2, data->sym2, '>');
			set_state(compare_id, tmp, NULL, new);
			break;
			}
		default:
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

		switch (state_to_comparison(state)) {
		case SPECIAL_EQUAL:
		case SPECIAL_LTE:
		case SPECIAL_UNSIGNED_LTE:
		case '<':
		case SPECIAL_UNSIGNED_LT: {
			struct compare_data *data = state->data;
			struct smatch_state *new;

			new = alloc_compare_state(data->var1, data->sym1, data->var2, data->sym2, '<');
			set_state(compare_id, tmp, NULL, new);
			break;
			}
		default:
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
		struct symbol *tmp_sym = left_sym;
		char *tmp_name = left;

		left = right;
		left_sym = right_sym;
		right = tmp_name;
		right_sym = tmp_sym;
		op = flip_op(expr->op);
	} else {
		op = expr->op;
	}
	false_op = falsify_op(op);
	snprintf(state_name, sizeof(state_name), "%s vs %s", left, right);
	true_state = alloc_compare_state(left, left_sym, right, right_sym, op);
	false_state = alloc_compare_state(left, left_sym, right, right_sym, false_op);

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
		struct symbol *tmp_sym = left_sym;
		char *tmp_name = left_name;

		left_name = right_name;
		left_sym = right_sym;
		right_name = tmp_name;
		right_sym = tmp_sym;
		comparison = flip_op(comparison);
	}
	snprintf(state_name, sizeof(state_name), "%s vs %s", left_name, right_name);
	state = alloc_compare_state(left_name, left_sym, right_name, right_sym, comparison);

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
		ret = state_to_comparison(state);

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

	if (!str_to_comparison_arg(c, call, &comparison, &arg))
		return;
	add_comparison(expr, SPECIAL_LTE, arg);
}

static char *range_comparison_to_param_helper(struct expression *expr, char starts_with)
{
	struct symbol *param;
	char *var = NULL;
	char buf[256];
	char *ret_str = NULL;
	int compare;
	int i;

	var = expr_to_var(expr);
	if (!var)
		goto free;

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, param) {
		i++;
		if (!param->ident)
			continue;
		snprintf(buf, sizeof(buf), "%s orig", param->ident->name);
		compare = get_comparison_strings(var, buf);
		if (!compare)
			continue;
		if (show_special(compare)[0] != starts_with)
			continue;
		snprintf(buf, sizeof(buf), "[%sp%d]", show_special(compare), i);
		ret_str = alloc_sname(buf);
		break;
	} END_FOR_EACH_PTR(param);

free:
	free_string(var);
	return ret_str;
}

char *expr_equal_to_param(struct expression *expr)
{
	return range_comparison_to_param_helper(expr, '=');
}

char *expr_lte_to_param(struct expression *expr)
{
	return range_comparison_to_param_helper(expr, '<');
}

static void free_data(struct symbol *sym)
{
	if (__inline_fn)
		return;
	clear_compare_data_alloc();
}

void register_comparison(int id)
{
	compare_id = id;
	add_hook(&match_logic, CONDITION_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&save_start_states, AFTER_DEF_HOOK);
	add_unmatched_state_hook(compare_id, unmatched_comparison);
	add_merge_hook(compare_id, &merge_compare_states);
	add_hook(&free_data, AFTER_FUNC_HOOK);
}

void register_comparison_links(int id)
{
	link_id = id;
	add_merge_hook(link_id, &merge_links);
	add_modification_hook(link_id, &match_modify);
}
