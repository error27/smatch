/*
 * Copyright (C) 2012 Oracle.
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
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int compare_id;
static int link_id;

struct compare_data {
	const char *var1;
	struct var_sym_list *vsl1;
	int comparison;
	const char *var2;
	struct var_sym_list *vsl2;
};
ALLOCATOR(compare_data, "compare data");

int chunk_vsl_eq(const char *a, struct var_sym_list *a_vsl, const char *b, struct var_sym_list *b_vsl)
{
	if (strcmp(a, b) == 0)
		return 1;
	return 0;
}

static struct symbol *vsl_to_sym(struct var_sym_list *vsl)
{
	struct var_sym *vs;

	if (!vsl)
		return NULL;
	if (ptr_list_size((struct ptr_list *)vsl) != 1)
		return NULL;
	vs = first_ptr_list((struct ptr_list *)vsl);
	return vs->sym;
}

static struct smatch_state *alloc_compare_state(
		const char *var1, struct var_sym_list *vsl1,
		int comparison,
		const char *var2, struct var_sym_list *vsl2)
{
	struct smatch_state *state;
	struct compare_data *data;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(show_special(comparison));
	data = __alloc_compare_data(0);
	data->var1 = alloc_sname(var1);
	data->vsl1 = clone_var_sym_list(vsl1);
	data->comparison = comparison;
	data->var2 = alloc_sname(var2);
	data->vsl2 = clone_var_sym_list(vsl2);
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

static struct range_list *get_orig_rl(struct var_sym_list *vsl)
{
	struct symbol *sym;
	struct smatch_state *state;

	if (!vsl)
		return NULL;
	sym = vsl_to_sym(vsl);
	if (!sym || !sym->ident)
		return NULL;
	state = get_orig_estate(sym->ident->name, sym);
	return estate_rl(state);
}

static struct smatch_state *unmatched_comparison(struct sm_state *sm)
{
	struct compare_data *data = sm->state->data;
	struct range_list *left_rl, *right_rl;
	int op;

	if (!data)
		return &undefined;

	if (strstr(data->var1, " orig"))
		left_rl = get_orig_rl(data->vsl1);
	else if (!get_implied_rl_var_sym(data->var1, vsl_to_sym(data->vsl1), &left_rl))
		return &undefined;
	if (strstr(data->var2, " orig"))
		right_rl = get_orig_rl(data->vsl2);
	else if (!get_implied_rl_var_sym(data->var2, vsl_to_sym(data->vsl2), &right_rl))
		return &undefined;


	op = rl_comparison(left_rl, right_rl);
	if (op)
		return alloc_compare_state(data->var1, data->vsl1, op, data->var2, data->vsl2);

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

/*
 * This is for when you merge states "a < b" and "a == b", the result is that
 * we can say for sure, "a <= b" after the merge.
 */
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

/*
 * This is for if you have "a < b" and "b <= c".  Or in other words,
 * "a < b <= c".  You would call this like get_combined_comparison('<', '<=').
 * The return comparison would be '<'.
 *
 * This function is different from merge_comparisons(), for example:
 * merge_comparison('<', '==') returns '<='
 * get_combined_comparison('<', '==') returns '<'
 */
static int combine_comparisons(int left_compare, int right_compare)
{
	int LT, EQ, GT;

	left_compare = remove_unsigned_from_comparison(left_compare);
	right_compare = remove_unsigned_from_comparison(right_compare);

	LT = EQ = GT = 0;

	switch (left_compare) {
	case '<':
		LT++;
		break;
	case SPECIAL_LTE:
		LT++;
		EQ++;
		break;
	case SPECIAL_EQUAL:
		return right_compare;
	case SPECIAL_GTE:
		GT++;
		EQ++;
		break;
	case '>':
		GT++;
	}

	switch (right_compare) {
	case '<':
		LT++;
		break;
	case SPECIAL_LTE:
		LT++;
		EQ++;
		break;
	case SPECIAL_EQUAL:
		return left_compare;
	case SPECIAL_GTE:
		GT++;
		EQ++;
		break;
	case '>':
		GT++;
	}

	if (LT == 2) {
		if (EQ == 2)
			return SPECIAL_LTE;
		return '<';
	}

	if (GT == 2) {
		if (EQ == 2)
			return SPECIAL_GTE;
		return '>';
	}
	return 0;
}

static struct smatch_state *merge_compare_states(struct smatch_state *s1, struct smatch_state *s2)
{
	struct compare_data *data = s1->data;
	int op;

	op = merge_comparisons(state_to_comparison(s1), state_to_comparison(s2));
	if (op)
		return alloc_compare_state(data->var1, data->vsl1, op, data->var2, data->vsl2);
	return &undefined;
}

static struct smatch_state *alloc_link_state(struct string_list *links)
{
	struct smatch_state *state;
	static char buf[256];
	char *tmp;
	int i;

	state = __alloc_smatch_state(0);

	i = 0;
	FOR_EACH_PTR(links, tmp) {
		if (!i++) {
			snprintf(buf, sizeof(buf), "%s", tmp);
		} else {
			append(buf, ", ", sizeof(buf));
			append(buf, tmp, sizeof(buf));
		}
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
		struct var_sym_list *vsl1 = NULL;
		struct var_sym_list *vsl2 = NULL;

		if (!param->ident)
			continue;
		snprintf(orig, sizeof(orig), "%s orig", param->ident->name);
		snprintf(state_name, sizeof(state_name), "%s vs %s", param->ident->name, orig);
		add_var_sym(&vsl1, param->ident->name, param);
		add_var_sym(&vsl2, orig, param);
		state = alloc_compare_state(param->ident->name, vsl1, SPECIAL_EQUAL, alloc_sname(orig), vsl2);
		set_state(compare_id, state_name, NULL, state);

		link = alloc_sname(state_name);
		links = NULL;
		insert_string(&links, link);
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

static void save_link_var_sym(const char *var, struct symbol *sym, const char *link)
{
	struct smatch_state *old_state, *new_state;
	struct string_list *links;
	char *new;

	old_state = get_state(link_id, var, sym);
	if (old_state)
		links = clone_str_list(old_state->data);
	else
		links = NULL;

	new = alloc_sname(link);
	insert_string(&links, new);

	new_state = alloc_link_state(links);
	set_state(link_id, var, sym, new_state);
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

			new = alloc_compare_state(data->var1, data->vsl1, '>', data->var2, data->vsl2);
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

			new = alloc_compare_state(data->var1, data->vsl1, '<', data->var2, data->vsl2);
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

	/* Huh???  This needs a comment!  */
	if (match_inc_dec(sm, mod_expr))
		return;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		set_state(compare_id, tmp, NULL, &undefined);
	} END_FOR_EACH_PTR(tmp);
	set_state(link_id, sm->name, sm->sym, &undefined);
}

static char *chunk_to_var_sym(struct expression *expr, struct symbol **sym)
{
	char *name, *left_name, *right_name;
	struct symbol *tmp;
	char buf[128];

	expr = strip_expr(expr);
	if (!expr)
		return NULL;
	if (sym)
		*sym = NULL;

	name = expr_to_var_sym(expr, &tmp);
	if (name && tmp) {
		if (sym)
			*sym = tmp;
		return name;
	}
	if (name)
		free_string(name);

	if (expr->type != EXPR_BINOP)
		return NULL;
	if (expr->op != '-' && expr->op != '+')
		return NULL;

	left_name = expr_to_var(expr->left);
	if (!left_name)
		return NULL;
	right_name = expr_to_var(expr->right);
	if (!right_name) {
		free_string(left_name);
		return NULL;
	}
	snprintf(buf, sizeof(buf), "%s %s %s", left_name, show_special(expr->op), right_name);
	free_string(left_name);
	free_string(right_name);
	return alloc_string(buf);
}

static char *chunk_to_var(struct expression *expr)
{
	return chunk_to_var_sym(expr, NULL);
}

static void save_link(struct expression *expr, char *link)
{
	char *var;
	struct symbol *sym;

	expr = strip_expr(expr);
	if (expr->type == EXPR_BINOP) {
		char *chunk;

		chunk = chunk_to_var(expr);
		if (!chunk)
			return;

		save_link(expr->left, link);
		save_link(expr->right, link);
		save_link_var_sym(chunk, NULL, link);
		return;
	}

	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto done;

	save_link_var_sym(var, sym, link);
done:
	free_string(var);
}

static void update_tf_links(struct stree *pre_stree,
			    const char *left_var, struct var_sym_list *left_vsl,
			    int left_comparison,
			    const char *mid_var, struct var_sym_list *mid_vsl,
			    struct string_list *links)
{
	struct smatch_state *state;
	struct smatch_state *true_state, *false_state;
	struct compare_data *data;
	const char *right_var;
	struct var_sym_list *right_vsl;
	int right_comparison;
	int true_comparison;
	int false_comparison;
	char *tmp;
	char state_name[256];
	struct var_sym *vs;

	FOR_EACH_PTR(links, tmp) {
		state = get_state_stree(pre_stree, compare_id, tmp, NULL);
		if (!state || !state->data)
			continue;
		data = state->data;
		right_comparison = data->comparison;
		right_var = data->var2;
		right_vsl = data->vsl2;
		if (chunk_vsl_eq(mid_var, mid_vsl, right_var, right_vsl)) {
			right_var = data->var1;
			right_vsl = data->vsl1;
			right_comparison = flip_op(right_comparison);
		}
		true_comparison = combine_comparisons(left_comparison, right_comparison);
		false_comparison = combine_comparisons(falsify_op(left_comparison), right_comparison);

		if (strcmp(left_var, right_var) > 0) {
			const char *tmp_var = left_var;
			struct var_sym_list *tmp_vsl = left_vsl;

			left_var = right_var;
			left_vsl = right_vsl;
			right_var = tmp_var;
			right_vsl = tmp_vsl;
			true_comparison = flip_op(true_comparison);
			false_comparison = flip_op(false_comparison);
		}

		if (!true_comparison && !false_comparison)
			continue;

		if (true_comparison)
			true_state = alloc_compare_state(left_var, left_vsl, true_comparison, right_var, right_vsl);
		else
			true_state = NULL;
		if (false_comparison)
			false_state = alloc_compare_state(left_var, left_vsl, false_comparison, right_var, right_vsl);
		else
			false_state = NULL;

		snprintf(state_name, sizeof(state_name), "%s vs %s", left_var, right_var);
		set_true_false_states(compare_id, state_name, NULL, true_state, false_state);
		FOR_EACH_PTR(left_vsl, vs) {
			save_link_var_sym(vs->var, vs->sym, state_name);
		} END_FOR_EACH_PTR(vs);
		FOR_EACH_PTR(right_vsl, vs) {
			save_link_var_sym(vs->var, vs->sym, state_name);
		} END_FOR_EACH_PTR(vs);
		if (!vsl_to_sym(left_vsl))
			save_link_var_sym(left_var, NULL, state_name);
		if (!vsl_to_sym(right_vsl))
			save_link_var_sym(right_var, NULL, state_name);
	} END_FOR_EACH_PTR(tmp);
}

static void update_tf_data(struct stree *pre_stree,
		const char *left_name, struct symbol *left_sym,
		const char *right_name, struct symbol *right_sym,
		struct compare_data *tdata)
{
	struct smatch_state *state;

	state = get_state_stree(pre_stree, link_id, tdata->var2, vsl_to_sym(tdata->vsl2));
	if (state)
		update_tf_links(pre_stree, tdata->var1, tdata->vsl1, tdata->comparison, tdata->var2, tdata->vsl2, state->data);

	state = get_state_stree(pre_stree, link_id, tdata->var1, vsl_to_sym(tdata->vsl1));
	if (state)
		update_tf_links(pre_stree, tdata->var2, tdata->vsl2, flip_op(tdata->comparison), tdata->var1, tdata->vsl1, state->data);
}

static void match_compare(struct expression *expr)
{
	char *left = NULL;
	char *right = NULL;
	struct symbol *left_sym, *right_sym;
	struct var_sym_list *left_vsl, *right_vsl;
	int op, false_op;
	struct smatch_state *true_state, *false_state;
	char state_name[256];
	struct stree *pre_stree;

	if (expr->type != EXPR_COMPARE)
		return;
	left = chunk_to_var_sym(expr->left, &left_sym);
	if (!left)
		goto free;
	left_vsl = expr_to_vsl(expr->left);
	right = chunk_to_var_sym(expr->right, &right_sym);
	if (!right)
		goto free;
	right_vsl = expr_to_vsl(expr->right);

	if (strcmp(left, right) > 0) {
		struct symbol *tmp_sym = left_sym;
		char *tmp_name = left;
		struct var_sym_list *tmp_vsl = left_vsl;

		left = right;
		left_sym = right_sym;
		left_vsl = right_vsl;
		right = tmp_name;
		right_sym = tmp_sym;
		right_vsl = tmp_vsl;
		op = flip_op(expr->op);
	} else {
		op = expr->op;
	}
	false_op = falsify_op(op);
	snprintf(state_name, sizeof(state_name), "%s vs %s", left, right);
	true_state = alloc_compare_state(left, left_vsl, op, right, right_vsl);
	false_state = alloc_compare_state(left, left_vsl, false_op, right, right_vsl);

	pre_stree = clone_stree(__get_cur_stree());
	update_tf_data(pre_stree, left, left_sym, right, right_sym, true_state->data);
	free_stree(&pre_stree);

	set_true_false_states(compare_id, state_name, NULL, true_state, false_state);
	save_link(expr->left, state_name);
	save_link(expr->right, state_name);
free:
	free_string(left);
	free_string(right);
}

static void add_comparison_var_sym(const char *left_name,
		struct var_sym_list *left_vsl,
		int comparison,
		const char *right_name, struct var_sym_list *right_vsl)
{
	struct smatch_state *state;
	struct var_sym *vs;
	char state_name[256];

	if (strcmp(left_name, right_name) > 0) {
		const char *tmp_name = left_name;
		struct var_sym_list *tmp_vsl = left_vsl;

		left_name = right_name;
		left_vsl = right_vsl;
		right_name = tmp_name;
		right_vsl = tmp_vsl;
		comparison = flip_op(comparison);
	}
	snprintf(state_name, sizeof(state_name), "%s vs %s", left_name, right_name);
	state = alloc_compare_state(left_name, left_vsl, comparison, right_name, right_vsl);

	set_state(compare_id, state_name, NULL, state);

	FOR_EACH_PTR(left_vsl, vs) {
		save_link_var_sym(vs->var, vs->sym, state_name);
	} END_FOR_EACH_PTR(vs);
	FOR_EACH_PTR(right_vsl, vs) {
		save_link_var_sym(vs->var, vs->sym, state_name);
	} END_FOR_EACH_PTR(vs);
}

static void add_comparison(struct expression *left, int comparison, struct expression *right)
{
	char *left_name = NULL;
	char *right_name = NULL;
	struct symbol *left_sym, *right_sym;
	struct var_sym_list *left_vsl, *right_vsl;
	struct smatch_state *state;
	char state_name[256];

	left_name = chunk_to_var_sym(left, &left_sym);
	if (!left_name)
		goto free;
	left_vsl = expr_to_vsl(left);
	right_name = chunk_to_var_sym(right, &right_sym);
	if (!right_name)
		goto free;
	right_vsl = expr_to_vsl(right);

	if (strcmp(left_name, right_name) > 0) {
		struct symbol *tmp_sym = left_sym;
		char *tmp_name = left_name;
		struct var_sym_list *tmp_vsl = left_vsl;

		left_name = right_name;
		left_sym = right_sym;
		left_vsl = right_vsl;
		right_name = tmp_name;
		right_sym = tmp_sym;
		right_vsl = tmp_vsl;
		comparison = flip_op(comparison);
	}
	snprintf(state_name, sizeof(state_name), "%s vs %s", left_name, right_name);
	state = alloc_compare_state(left_name, left_vsl, comparison, right_name, right_vsl);

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

	get_absolute_min(r_left, &left_tmp);
	get_absolute_min(r_right, &right_tmp);

	if (left_tmp.value > 0)
		add_comparison(expr->left, '>', r_right);
	else if (left_tmp.value == 0)
		add_comparison(expr->left, SPECIAL_GTE, r_right);

	if (right_tmp.value > 0)
		add_comparison(expr->left, '>', r_left);
	else if (right_tmp.value == 0)
		add_comparison(expr->left, SPECIAL_GTE, r_left);
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

static void match_assign_divide(struct expression *expr)
{
	struct expression *right;
	struct expression *r_left, *r_right;
	sval_t min;

	right = strip_expr(expr->right);
	r_left = strip_expr(right->left);
	r_right = strip_expr(right->right);
	if (!get_implied_min(r_right, &min) || min.value <= 1)
		return;

	add_comparison(expr->left, '<', r_left);
}

static void match_binop_assign(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (right->op == '+')
		match_assign_add(expr);
	if (right->op == '-')
		match_assign_sub(expr);
	if (right->op == '/')
		match_assign_divide(expr);
}

static void copy_comparisons(struct expression *left, struct expression *right)
{
	struct string_list *links;
	struct smatch_state *state;
	struct compare_data *data;
	struct symbol *left_sym, *right_sym;
	char *left_var = NULL;
	char *right_var = NULL;
	struct var_sym_list *left_vsl;
	const char *var;
	struct var_sym_list *vsl;
	int comparison;
	char *tmp;

	left_var = chunk_to_var_sym(left, &left_sym);
	if (!left_var)
		goto done;
	left_vsl = expr_to_vsl(left);
	right_var = chunk_to_var_sym(right, &right_sym);
	if (!right_var)
		goto done;

	state = get_state(link_id, right_var, right_sym);
	if (!state)
		return;
	links = state->data;

	FOR_EACH_PTR(links, tmp) {
		state = get_state(compare_id, tmp, NULL);
		if (!state || !state->data)
			continue;
		data = state->data;
		comparison = data->comparison;
		var = data->var2;
		vsl = data->vsl2;
		if (chunk_vsl_eq(var, vsl, right_var, NULL)) {
			var = data->var1;
			vsl = data->vsl1;
			comparison = flip_op(comparison);
		}
		add_comparison_var_sym(left_var, left_vsl, comparison, var, vsl);
	} END_FOR_EACH_PTR(tmp);

done:
	free_string(right_var);
}

static void match_assign(struct expression *expr)
{
	struct expression *right;

	if (expr->op != '=')
		return;

	if (is_struct(expr->left))
		return;

	copy_comparisons(expr->left, expr->right);
	add_comparison(expr->left, SPECIAL_EQUAL, expr->right);

	right = strip_expr(expr->right);
	if (right->type == EXPR_BINOP)
		match_binop_assign(expr);
}

int get_comparison_strings(const char *one, const char *two)
{
	char buf[256];
	struct smatch_state *state;
	int invert = 0;
	int ret = 0;

	if (strcmp(one, two) == 0)
		return SPECIAL_EQUAL;

	if (strcmp(one, two) > 0) {
		const char *tmp = one;

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

	one = chunk_to_var(a);
	if (!one)
		goto free;
	two = chunk_to_var(b);
	if (!two)
		goto free;

	ret = get_comparison_strings(one, two);
free:
	free_string(one);
	free_string(two);
	return ret;
}

int possible_comparison(struct expression *a, int comparison, struct expression *b)
{
	char *one = NULL;
	char *two = NULL;
	int ret = 0;
	char buf[256];
	struct sm_state *sm;
	int saved;

	one = chunk_to_var(a);
	if (!one)
		goto free;
	two = chunk_to_var(b);
	if (!two)
		goto free;


	if (strcmp(one, two) == 0 && comparison == SPECIAL_EQUAL) {
		ret = 1;
		goto free;
	}

	if (strcmp(one, two) > 0) {
		char *tmp = one;

		one = two;
		two = tmp;
		comparison = flip_op(comparison);
	}

	snprintf(buf, sizeof(buf), "%s vs %s", one, two);
	sm = get_sm_state(compare_id, buf, NULL);
	if (!sm)
		goto free;

	FOR_EACH_PTR(sm->possible, sm) {
		if (!sm->state->data)
			continue;
		saved = ((struct compare_data *)sm->state->data)->comparison;
		if (saved == comparison)
			ret = 1;
		if (comparison == SPECIAL_EQUAL &&
		    (saved == SPECIAL_LTE ||
		     saved == SPECIAL_GTE ||
		     saved == SPECIAL_UNSIGNED_LTE ||
		     saved == SPECIAL_UNSIGNED_GTE))
			ret = 1;
		if (ret == 1)
			goto free;
	} END_FOR_EACH_PTR(sm);

	return ret;
free:
	free_string(one);
	free_string(two);
	return ret;
}

static void update_links_from_call(struct expression *left,
				   int left_compare,
				   struct expression *right)
{
	struct string_list *links;
	struct smatch_state *state;
	struct compare_data *data;
	struct symbol *left_sym, *right_sym;
	char *left_var = NULL;
	char *right_var = NULL;
	struct var_sym_list *left_vsl;
	const char *var;
	struct var_sym_list *vsl;
	int comparison;
	char *tmp;

	left_var = chunk_to_var_sym(left, &left_sym);
	if (!left_var)
		goto done;
	left_vsl = expr_to_vsl(left);
	right_var = chunk_to_var_sym(right, &right_sym);
	if (!right_var)
		goto done;

	state = get_state(link_id, right_var, right_sym);
	if (!state)
		return;
	links = state->data;

	FOR_EACH_PTR(links, tmp) {
		state = get_state(compare_id, tmp, NULL);
		if (!state || !state->data)
			continue;
		data = state->data;
		comparison = data->comparison;
		var = data->var2;
		vsl = data->vsl2;
		if (chunk_vsl_eq(var, vsl, right_var, NULL)) {
			var = data->var1;
			vsl = data->vsl1;
			comparison = flip_op(comparison);
		}
		comparison = combine_comparisons(left_compare, comparison);
		if (!comparison)
			continue;
		add_comparison_var_sym(left_var, left_vsl, comparison, var, vsl);
	} END_FOR_EACH_PTR(tmp);

done:
	free_string(right_var);
}

void __add_comparison_info(struct expression *expr, struct expression *call, const char *range)
{
	struct expression *arg;
	int comparison;
	const char *c = range;

	if (!str_to_comparison_arg(c, call, &comparison, &arg))
		return;
	update_links_from_call(expr, comparison, arg);
	add_comparison(expr, comparison, arg);
}

static char *range_comparison_to_param_helper(struct expression *expr, char starts_with, int ignore)
{
	struct symbol *param;
	char *var = NULL;
	char buf[256];
	char *ret_str = NULL;
	int compare;
	int i;

	var = chunk_to_var(expr);
	if (!var)
		goto free;

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, param) {
		i++;
		if (i == ignore)
			continue;
		if (!param->ident)
			continue;
		snprintf(buf, sizeof(buf), "%s orig", param->ident->name);
		compare = get_comparison_strings(var, buf);
		if (!compare)
			continue;
		if (show_special(compare)[0] != starts_with)
			continue;
		snprintf(buf, sizeof(buf), "[%s$%d]", show_special(compare), i);
		ret_str = alloc_sname(buf);
		break;
	} END_FOR_EACH_PTR(param);

free:
	free_string(var);
	return ret_str;
}

char *expr_equal_to_param(struct expression *expr, int ignore)
{
	return range_comparison_to_param_helper(expr, '=', -1);
}

char *expr_lte_to_param(struct expression *expr, int ignore)
{
	return range_comparison_to_param_helper(expr, '<', ignore);
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
	add_hook(&match_compare, CONDITION_HOOK);
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
