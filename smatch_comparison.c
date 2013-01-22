
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

static void insert_string(struct string_list **str_list, char *new)
{
	char *tmp;

	FOR_EACH_PTR(*str_list, tmp) {
		if (strcmp(tmp, new) < 0)
			continue;
		else if (strcmp(tmp, new) == 0) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(str_list, new);
}

struct string_list *clone_str_list(struct string_list *orig)
{
	char *tmp;
	struct string_list *ret = NULL;

	FOR_EACH_PTR(orig, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static struct string_list *combine_string_lists(struct string_list *one, struct string_list *two)
{
	struct string_list *ret;
	char *tmp;

	ret = clone_str_list(one);
	FOR_EACH_PTR(two, tmp) {
		insert_string(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
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

static void clear_links(struct sm_state *sm)
{
	struct string_list *links;
	char *tmp;

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

int get_comparison(struct expression *a, struct expression *b)
{
	char *one = NULL;
	char *two = NULL;
	char buf[256];
	struct smatch_state *state;
	int invert = 0;
	int ret = 0;

	one = expr_to_var(a);
	if (!one)
		goto free;
	two = expr_to_var(b);
	if (!two)
		goto free;

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

free:
	free_string(one);
	free_string(two);
	return ret;

}

void register_comparison(int id)
{
	compare_id = id;
	add_hook(&match_logic, CONDITION_HOOK);
}

void register_comparison_links(int id)
{
	link_id = id;
	add_merge_hook(link_id, &merge_func);
	add_modification_hook(link_id, &clear_links);
}
