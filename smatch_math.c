/*
 * smatch/smatch_math.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "symbol.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static struct range_list *_get_rl(struct expression *expr, int implied);
static struct range_list *handle_variable(struct expression *expr, int implied);

static sval_t zero  = {.type = &int_ctype, {.value = 0} };
static sval_t one   = {.type = &int_ctype, {.value = 1} };

static struct range_list *rl_zero(void)
{
	return alloc_rl(zero, zero);
}

static struct range_list *rl_one(void)
{
	return alloc_rl(one, one);
}

enum {
	RL_EXACT,
	RL_HARD,
	RL_FUZZY,
	RL_IMPLIED,
	RL_ABSOLUTE
};

static struct range_list *last_stmt_rl(struct statement *stmt, int implied)
{
	if (!stmt)
		return NULL;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return NULL;
	return _get_rl(stmt->expression, implied);
}

static struct range_list *handle_expression_statement_rl(struct expression *expr, int implied)
{
	return last_stmt_rl(get_expression_statement(expr), implied);
}

static struct range_list *handle_ampersand_rl(int implied)
{
	if (implied == RL_EXACT || implied == RL_HARD)
		return NULL;
	return alloc_rl(valid_ptr_min_sval, valid_ptr_max_sval);
}

static struct range_list *handle_negate_rl(struct expression *expr, int implied)
{
	if (known_condition_true(expr->unop))
		return rl_zero();
	if (known_condition_false(expr->unop))
		return rl_one();

	if (implied == RL_EXACT)
		return NULL;

	if (implied_condition_true(expr->unop))
		return rl_zero();
	if (implied_condition_false(expr->unop))
		return rl_one();
	return alloc_rl(zero, one);
}

static struct range_list *handle_bitwise_negate(struct expression *expr, int implied)
{
	struct range_list *rl;
	sval_t sval;

	rl = _get_rl(expr->unop, implied);
	if (!rl_to_sval(rl, &sval))
		return NULL;
	sval = sval_preop(sval, '~');
	sval_cast(get_type(expr->unop), sval);
	return alloc_rl(sval, sval);
}

static struct range_list *handle_minus_preop(struct expression *expr, int implied)
{
	struct range_list *rl;
	sval_t sval;

	rl = _get_rl(expr->unop, implied);
	if (!rl_to_sval(rl, &sval))
		return NULL;
	sval = sval_preop(sval, '-');
	return alloc_rl(sval, sval);
}

static struct range_list *handle_preop_rl(struct expression *expr, int implied)
{
	switch (expr->op) {
	case '&':
		return handle_ampersand_rl(implied);
	case '!':
		return handle_negate_rl(expr, implied);
	case '~':
		return handle_bitwise_negate(expr, implied);
	case '-':
		return handle_minus_preop(expr, implied);
	case '*':
		return handle_variable(expr, implied);
	case '(':
		return handle_expression_statement_rl(expr, implied);
	default:
		return NULL;
	}
}

static struct range_list *handle_divide_rl(struct expression *expr, int implied)
{
	struct range_list *left_rl, *right_rl;
	struct symbol *type;
	sval_t min, max;

	type = get_type(expr);

	left_rl = _get_rl(expr->left, implied);
	left_rl = cast_rl(type, left_rl);
	right_rl = _get_rl(expr->right, implied);
	right_rl = cast_rl(type, right_rl);

	if (!left_rl || !right_rl)
		return NULL;
	if (is_whole_rl(left_rl) || is_whole_rl(right_rl))
		return NULL;
	if (sval_is_negative(rl_min(left_rl)) || sval_cmp_val(rl_min(right_rl), 0) <= 0)
		return NULL;

	max = rl_max(left_rl);
	if (!sval_is_max(max))
		max = sval_binop(max, '/', rl_min(right_rl));
	min = sval_binop(rl_min(left_rl), '/', rl_max(right_rl));
	return alloc_rl(min, max);
}

static struct range_list *handle_subtract_rl(struct expression *expr, int implied)
{
	struct symbol *type;
	struct range_list *left_rl, *right_rl;
	sval_t max, min, tmp;
	int comparison;

	type = get_type(expr);
	comparison = get_comparison(expr->left, expr->right);

	left_rl = _get_rl(expr->left, implied);
	left_rl = cast_rl(type, left_rl);
	right_rl = _get_rl(expr->right, implied);
	right_rl = cast_rl(type, right_rl);

	if ((!left_rl || !right_rl) &&
	    (implied == RL_EXACT || implied == RL_HARD || implied == RL_FUZZY))
		return NULL;

	if (!left_rl)
		left_rl = alloc_whole_rl(type);
	if (!right_rl)
		right_rl = alloc_whole_rl(type);

	/* negative values complicate everything fix this later */
	if (sval_is_negative(rl_min(right_rl)))
		return NULL;
	max = rl_max(left_rl);

	switch (comparison) {
	case '>':
	case SPECIAL_UNSIGNED_GT:
		min = sval_type_val(type, 1);
		max = rl_max(left_rl);
		break;
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		min = sval_type_val(type, 0);
		max = rl_max(left_rl);
		break;
	default:
		if (sval_binop_overflows(rl_min(left_rl), '-', rl_max(right_rl)))
			return NULL;
		min = sval_type_min(type);
	}

	if (!sval_binop_overflows(rl_min(left_rl), '-', rl_max(right_rl))) {
		tmp = sval_binop(rl_min(left_rl), '-', rl_max(right_rl));
		if (sval_cmp(tmp, min) > 0)
			min = tmp;
	}

	if (!sval_is_max(rl_max(left_rl))) {
		tmp = sval_binop(rl_max(left_rl), '-', rl_min(right_rl));
		if (sval_cmp(tmp, max) < 0)
			max = tmp;
	}

	if (sval_is_min(min) && sval_is_max(max))
		return NULL;

	return cast_rl(type, alloc_rl(min, max));
}

static struct range_list *handle_mod_rl(struct expression *expr, int implied)
{
	struct range_list *rl;
	sval_t left, right, sval;

	if (implied == RL_EXACT) {
		if (!get_value(expr->right, &right))
			return NULL;
		if (!get_value(expr->left, &left))
			return NULL;
		sval = sval_binop(left, '%', right);
		return alloc_rl(sval, sval);
	}
	/* if we can't figure out the right side it's probably hopeless */
	if (!get_implied_value(expr->right, &right))
		return NULL;

	right = sval_cast(get_type(expr), right);
	right.value--;

	rl = _get_rl(expr->left, implied);
	if (rl && rl_max(rl).uvalue < right.uvalue)
		right.uvalue = rl_max(rl).uvalue;

	return alloc_rl(zero, right);
}

static sval_t sval_lowest_set_bit(sval_t sval)
{
	int i;
	int found = 0;

	for (i = 0; i < 64; i++) {
		if (sval.uvalue & 1ULL << i) {
			if (!found++)
				continue;
			sval.uvalue &= ~(1ULL << i);
		}
	}
	return sval;
}

static struct range_list *handle_bitwise_AND(struct expression *expr, int implied)
{
	struct symbol *type;
	struct range_list *left_rl, *right_rl;
	sval_t known;

	if (implied != RL_IMPLIED && implied != RL_ABSOLUTE)
		return NULL;

	type = get_type(expr);

	if (get_implied_value(expr->left, &known)) {
		sval_t min;

		min = sval_lowest_set_bit(known);
		left_rl = alloc_rl(min, known);
		left_rl = cast_rl(type, left_rl);
		add_range(&left_rl, sval_type_val(type, 0), sval_type_val(type, 0));
	} else {
		left_rl = _get_rl(expr->left, implied);
		if (left_rl) {
			left_rl = cast_rl(type, left_rl);
			left_rl = alloc_rl(sval_type_val(type, 0), rl_max(left_rl));
		} else {
			if (implied == RL_HARD)
				return NULL;
			left_rl = alloc_whole_rl(type);
		}
	}

	if (get_implied_value(expr->right, &known)) {
		sval_t min;

		min = sval_lowest_set_bit(known);
		right_rl = alloc_rl(min, known);
		right_rl = cast_rl(type, right_rl);
		add_range(&right_rl, sval_type_val(type, 0), sval_type_val(type, 0));
	} else {
		right_rl = _get_rl(expr->right, implied);
		if (right_rl) {
			right_rl = cast_rl(type, right_rl);
			right_rl = alloc_rl(sval_type_val(type, 0), rl_max(right_rl));
		} else {
			if (implied == RL_HARD)
				return NULL;
			right_rl = alloc_whole_rl(type);
		}
	}

	return rl_intersection(left_rl, right_rl);
}

static struct range_list *handle_right_shift(struct expression *expr, int implied)
{
	struct range_list *left_rl;
	sval_t right;
	sval_t min, max;

	if (implied == RL_EXACT || implied == RL_HARD)
		return NULL;
	/* this is hopeless without the right side */
	if (!get_implied_value(expr->right, &right))
		return NULL;
	left_rl = _get_rl(expr->left, implied);
	if (left_rl) {
		max = rl_max(left_rl);
		min = rl_min(left_rl);
	} else {
		if (implied == RL_FUZZY)
			return NULL;
		max = sval_type_max(get_type(expr->left));
		min = sval_type_val(get_type(expr->left), 0);
	}

	max = sval_binop(max, SPECIAL_RIGHTSHIFT, right);
	min = sval_binop(min, SPECIAL_RIGHTSHIFT, right);
	return alloc_rl(min, max);
}

static struct range_list *handle_known_binop(struct expression *expr)
{
	sval_t left, right;

	if (!get_value(expr->left, &left))
		return NULL;
	if (!get_value(expr->right, &right))
		return NULL;
	left = sval_binop(left, expr->op, right);
	return alloc_rl(left, left);
}

static struct range_list *handle_binop_rl(struct expression *expr, int implied)
{
	struct symbol *type;
	struct range_list *left_rl, *right_rl, *rl;
	sval_t min, max;

	rl = handle_known_binop(expr);
	if (rl)
		return rl;
	if (implied == RL_EXACT)
		return NULL;

	switch (expr->op) {
	case '%':
		return handle_mod_rl(expr, implied);
	case '&':
		return handle_bitwise_AND(expr, implied);
	case SPECIAL_RIGHTSHIFT:
		return handle_right_shift(expr, implied);
	case '-':
		return handle_subtract_rl(expr, implied);
	case '/':
		return handle_divide_rl(expr, implied);
	}

	type = get_type(expr);
	left_rl = _get_rl(expr->left, implied);
	left_rl = cast_rl(type, left_rl);
	right_rl = _get_rl(expr->right, implied);
	right_rl = cast_rl(type, right_rl);

	if (!left_rl || !right_rl)
		return NULL;

	if (sval_binop_overflows(rl_min(left_rl), expr->op, rl_min(right_rl)))
		return NULL;
	min = sval_binop(rl_min(left_rl), expr->op, rl_min(right_rl));

	if (sval_binop_overflows(rl_max(left_rl), expr->op, rl_max(right_rl))) {
		switch (implied) {
		case RL_FUZZY:
		case RL_HARD:
			return NULL;
		default:
			max = sval_type_max(get_type(expr));
		}
	} else {
		max = sval_binop(rl_max(left_rl), expr->op, rl_max(right_rl));
	}

	return alloc_rl(min, max);
}

static int do_comparison(struct expression *expr)
{
	struct range_list *left_ranges = NULL;
	struct range_list *right_ranges = NULL;
	int poss_true, poss_false;
	struct symbol *type;

	type = get_type(expr);

	get_implied_rl(expr->left, &left_ranges);
	get_implied_rl(expr->right, &right_ranges);
	left_ranges = cast_rl(type, left_ranges);
	right_ranges = cast_rl(type, right_ranges);

	poss_true = possibly_true_rl(left_ranges, expr->op, right_ranges);
	poss_false = possibly_false_rl(left_ranges, expr->op, right_ranges);

	free_rl(&left_ranges);
	free_rl(&right_ranges);

	if (!poss_true && !poss_false)
		return 0;
	if (poss_true && !poss_false)
		return 1;
	if (!poss_true && poss_false)
		return 2;
	return 3;
}

static struct range_list *handle_comparison_rl(struct expression *expr, int implied)
{
	sval_t left, right;
	int res;

	if (get_value(expr->left, &left) && get_value(expr->right, &right)) {
		struct data_range tmp_left, tmp_right;

		tmp_left.min = left;
		tmp_left.max = left;
		tmp_right.min = right;
		tmp_right.max = right;
		if (true_comparison_range(&tmp_left, expr->op, &tmp_right))
			return rl_one();
		return rl_zero();
	}

	if (implied == RL_EXACT)
		return NULL;

	res = do_comparison(expr);
	if (res == 1)
		return rl_one();
	if (res == 2)
		return rl_zero();

	return alloc_rl(zero, one);
}

static struct range_list *handle_logical_rl(struct expression *expr, int implied)
{
	sval_t left, right;
	int left_known = 0;
	int right_known = 0;

	if (implied == RL_EXACT) {
		if (get_value(expr->left, &left))
			left_known = 1;
		if (get_value(expr->right, &right))
			right_known = 1;
	} else {
		if (get_implied_value(expr->left, &left))
			left_known = 1;
		if (get_implied_value(expr->right, &right))
			right_known = 1;
	}

	switch (expr->op) {
	case SPECIAL_LOGICAL_OR:
		if (left_known && left.value)
			return rl_one();
		if (right_known && right.value)
			return rl_one();
		if (left_known && right_known)
			return rl_zero();
		break;
	case SPECIAL_LOGICAL_AND:
		if (left_known && right_known) {
			if (left.value && right.value)
				return rl_one();
			return rl_zero();
		}
		break;
	default:
		return NULL;
	}

	if (implied == RL_EXACT)
		return NULL;

	return alloc_rl(zero, one);
}

static struct range_list *handle_conditional_rl(struct expression *expr, int implied)
{
	struct range_list *true_rl, *false_rl;
	struct symbol *type;
	int final_pass_orig = final_pass;

	if (known_condition_true(expr->conditional))
		return _get_rl(expr->cond_true, implied);
	if (known_condition_false(expr->conditional))
		return _get_rl(expr->cond_false, implied);

	if (implied == RL_EXACT)
		return NULL;

	if (implied_condition_true(expr->conditional))
		return _get_rl(expr->cond_true, implied);
	if (implied_condition_false(expr->conditional))
		return _get_rl(expr->cond_false, implied);


	/* this becomes a problem with deeply nested conditional statements */
	if (low_on_memory())
		return NULL;

	type = get_type(expr);

	__push_fake_cur_slist();
	final_pass = 0;
	__split_whole_condition(expr->conditional);
	true_rl = _get_rl(expr->cond_true, implied);
	__push_true_states();
	__use_false_states();
	false_rl = _get_rl(expr->cond_false, implied);
	__merge_true_states();
	__free_fake_cur_slist();
	final_pass = final_pass_orig;

	if (!true_rl || !false_rl)
		return NULL;
	true_rl = cast_rl(type, true_rl);
	false_rl = cast_rl(type, false_rl);

	return rl_union(true_rl, false_rl);
}

static int get_fuzzy_max_helper(struct expression *expr, sval_t *max)
{
	struct sm_state *sm;
	struct sm_state *tmp;
	sval_t sval;

	if (get_hard_max(expr, &sval)) {
		*max = sval;
		return 1;
	}

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	sval = sval_type_min(estate_type(sm->state));
	FOR_EACH_PTR(sm->possible, tmp) {
		sval_t new_min;

		new_min = estate_min(tmp->state);
		if (sval_cmp(new_min, sval) > 0)
			sval = new_min;
	} END_FOR_EACH_PTR(tmp);

	if (sval_is_min(sval))
		return 0;
	if (sval.value == sval_type_min(sval.type).value + 1)  /* it's common to be on off */
		return 0;

	*max = sval_cast(get_type(expr), sval);
	return 1;
}

static int get_fuzzy_min_helper(struct expression *expr, sval_t *min)
{
	struct sm_state *sm;
	struct sm_state *tmp;
	sval_t sval;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	if (!sval_is_min(estate_min(sm->state))) {
		*min = estate_min(sm->state);
		return 1;
	}

	sval = sval_type_max(estate_type(sm->state));
	FOR_EACH_PTR(sm->possible, tmp) {
		sval_t new_max;

		new_max = estate_max(tmp->state);
		if (sval_cmp(new_max, sval) < 0)
			sval = new_max;
	} END_FOR_EACH_PTR(tmp);

	if (sval_is_max(sval))
		return 0;
	*min = sval_cast(get_type(expr), sval);
	return 1;
}

static int get_const_value(struct expression *expr, sval_t *sval)
{
	struct symbol *sym;
	sval_t right;

	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;
	sym = expr->symbol;
	if (!(sym->ctype.modifiers & MOD_CONST))
		return 0;
	if (get_value(sym->initializer, &right)) {
		*sval = sval_cast(get_type(expr), right);
		return 1;
	}
	return 0;
}

static struct range_list *handle_variable(struct expression *expr, int implied)
{
	struct smatch_state *state;
	struct range_list *rl;
	sval_t sval, min, max;

	if (get_const_value(expr, &sval))
		return alloc_rl(sval, sval);

	switch (implied) {
	case RL_EXACT:
		return NULL;
	case RL_HARD:
	case RL_IMPLIED:
	case RL_ABSOLUTE:
		state = get_state_expr(SMATCH_EXTRA, expr);
		if (!state || !state->data) {
			if (implied == RL_HARD)
				return NULL;
			if (get_local_rl(expr, &rl))
				return rl;
			return NULL;
		}
		if (implied == RL_HARD && !estate_has_hard_max(state))
			return NULL;
		return clone_rl(estate_rl(state));
	case RL_FUZZY:
		if (!get_fuzzy_min_helper(expr, &min))
			min = sval_type_min(get_type(expr));
		if (!get_fuzzy_max_helper(expr, &max))
			return NULL;
		/* fuzzy ranges are often inverted */
		if (sval_cmp(min, max) > 0) {
			sval = min;
			min = max;
			max = sval;
		}
		return alloc_rl(min, max);
	}
	return NULL;
}

static sval_t handle_sizeof(struct expression *expr)
{
	struct symbol *sym;
	sval_t ret;

	ret = sval_blank(expr);
	sym = expr->cast_type;
	if (!sym) {
		sym = evaluate_expression(expr->cast_expression);
		/*
		 * Expressions of restricted types will possibly get
		 * promoted - check that here
		 */
		if (is_restricted_type(sym)) {
			if (sym->bit_size < bits_in_int)
				sym = &int_ctype;
		} else if (is_fouled_type(sym)) {
			sym = &int_ctype;
		}
	}
	examine_symbol_type(sym);

	ret.type = size_t_ctype;
	if (sym->bit_size <= 0) /* sizeof(void) */ {
		if (get_real_base_type(sym) == &void_ctype)
			ret.value = 1;
		else
			ret.value = 0;
	} else
		ret.value = bits_to_bytes(sym->bit_size);

	return ret;
}

static struct range_list *handle_call_rl(struct expression *expr, int implied)
{
	struct range_list *rl;

	if (implied == RL_EXACT || implied == RL_HARD || implied == RL_FUZZY)
		return NULL;

	if (get_implied_return(expr, &rl))
		return rl;
	return db_return_vals(expr);
}

static struct range_list *handle_cast(struct expression *expr, int implied)
{
	struct range_list *rl;
	struct symbol *type;

	type = get_type(expr);
	rl = _get_rl(expr->cast_expression, implied);
	if (rl)
		return cast_rl(type, rl);
	if (implied == RL_ABSOLUTE)
		return alloc_whole_rl(type);
	if (implied == RL_IMPLIED && type &&
	    type->bit_size > 0 && type->bit_size < 32)
		return alloc_whole_rl(type);
	return NULL;
}

static struct range_list *_get_rl(struct expression *expr, int implied)
{
	struct range_list *rl;
	struct symbol *type;
	sval_t sval;

	expr = strip_parens(expr);
	if (!expr)
		return NULL;
	type = get_type(expr);

	switch (expr->type) {
	case EXPR_VALUE:
		sval = sval_from_val(expr, expr->value);
		rl = alloc_rl(sval, sval);
		break;
	case EXPR_PREOP:
		rl = handle_preop_rl(expr, implied);
		break;
	case EXPR_POSTOP:
		rl = _get_rl(expr->unop, implied);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		rl = handle_cast(expr, implied);
		break;
	case EXPR_BINOP:
		rl = handle_binop_rl(expr, implied);
		break;
	case EXPR_COMPARE:
		rl = handle_comparison_rl(expr, implied);
		break;
	case EXPR_LOGICAL:
		rl = handle_logical_rl(expr, implied);
		break;
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		sval = handle_sizeof(expr);
		rl = alloc_rl(sval, sval);
		break;
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		rl = handle_conditional_rl(expr, implied);
		break;
	case EXPR_CALL:
		rl = handle_call_rl(expr, implied);
		break;
	default:
		rl = handle_variable(expr, implied);
	}

	if (rl)
		return rl;
	if (type && implied == RL_ABSOLUTE)
		return alloc_whole_rl(type);
	return NULL;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl = _get_rl(expr, RL_EXACT);
	if (!rl_to_sval(rl, sval))
		return 0;
	return 1;
}

int get_implied_value(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, RL_IMPLIED);
	if (!rl_to_sval(rl, sval))
		return 0;
	return 1;
}

int get_implied_min(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, RL_IMPLIED);
	if (!rl)
		return 0;
	*sval = rl_min(rl);
	return 1;
}

int get_implied_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, RL_IMPLIED);
	if (!rl)
		return 0;
	*sval = rl_max(rl);
	return 1;
}

int get_implied_rl(struct expression *expr, struct range_list **rl)
{
	*rl = _get_rl(expr, RL_IMPLIED);
	if (*rl)
		return 1;
	return 0;
}

int get_absolute_rl(struct expression *expr, struct range_list **rl)
{
	*rl = _get_rl(expr, RL_ABSOLUTE);
	if (!*rl)
		*rl = alloc_whole_rl(get_type(expr));
	return 1;
}

int get_implied_rl_var_sym(const char *var, struct symbol *sym, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state(SMATCH_EXTRA, var, sym);
	*rl = estate_rl(state);
	if (*rl)
		return 1;
	return 0;
}

int get_hard_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, RL_HARD);
	if (!rl)
		return 0;
	*sval = rl_max(rl);
	return 1;
}

int get_fuzzy_min(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;
	sval_t tmp;

	rl =  _get_rl(expr, RL_FUZZY);
	if (!rl)
		return 0;
	tmp = rl_min(rl);
	if (sval_is_negative(tmp) && sval_is_min(tmp))
		return 0;
	*sval = tmp;
	return 1;
}

int get_fuzzy_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;
	sval_t max;

	rl =  _get_rl(expr, RL_FUZZY);
	if (!rl)
		return 0;
	max = rl_max(rl);
	if (max.uvalue > INT_MAX - 10000)
		return 0;
	*sval = max;
	return 1;
}

int get_absolute_min(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		type = &llong_ctype;  // FIXME: this is wrong but places assume get type can't fail.
	rl = _get_rl(expr, RL_ABSOLUTE);
	if (rl)
		*sval = rl_min(rl);
	else
		*sval = sval_type_min(type);

	if (sval_cmp(*sval, sval_type_min(type)) < 0)
		*sval = sval_type_min(type);
	return 1;
}

int get_absolute_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		type = &llong_ctype;
	rl = _get_rl(expr, RL_ABSOLUTE);
	if (rl)
		*sval = rl_max(rl);
	else
		*sval = sval_type_max(type);

	if (sval_cmp(sval_type_max(type), *sval) < 0)
		*sval = sval_type_max(type);
	return 1;
}

int known_condition_true(struct expression *expr)
{
	sval_t tmp;

	if (!expr)
		return 0;

	if (get_value(expr, &tmp) && tmp.value)
		return 1;

	return 0;
}

int known_condition_false(struct expression *expr)
{
	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	if (expr->type == EXPR_CALL) {
		if (sym_name_is("__builtin_constant_p", expr->fn))
			return 1;
	}
	return 0;
}

int implied_condition_true(struct expression *expr)
{
	sval_t tmp;

	if (!expr)
		return 0;

	if (known_condition_true(expr))
		return 1;
	if (get_implied_value(expr, &tmp) && tmp.value)
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
	struct expression *tmp;
	sval_t sval;

	if (!expr)
		return 0;

	if (known_condition_false(expr))
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
		tmp = strip_expr(expr);
		if (tmp != expr)
			return implied_condition_false(tmp);
		break;
	default:
		if (get_implied_value(expr, &sval) && sval.value == 0)
			return 1;
		break;
	}
	return 0;
}
