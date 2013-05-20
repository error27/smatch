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
static sval_t _get_value(struct expression *expr, int *undefined, int implied);
static sval_t _get_implied_value(struct expression *expr, int *undefined, int implied);

#define BOGUS 12345

static sval_t zero  = {.type = &int_ctype, {.value = 0} };
static sval_t one   = {.type = &int_ctype, {.value = 1} };
static sval_t bogus = {.type = &int_ctype, {.value = BOGUS} };

static struct range_list *rl_zero(void)
{
	return alloc_rl(zero, zero);
}

static struct range_list *rl_one(void)
{
	return alloc_rl(one, one);
}

enum {
	EXACT,
	IMPLIED,
	IMPLIED_MIN,
	IMPLIED_MAX,
	FUZZY_MAX,
	FUZZY_MIN,
	ABSOLUTE_MIN,
	ABSOLUTE_MAX,
	HARD_MAX,
};

enum {
	RL_EXACT,
	RL_HARD,
	RL_FUZZY,
	RL_IMPLIED,
	RL_ABSOLUTE
};

static int implied_to_rl_enum(int implied)
{
	switch (implied) {
	case EXACT:
		return RL_EXACT;
	case HARD_MAX:
		return RL_HARD;
	case FUZZY_MAX:
	case FUZZY_MIN:
		return RL_FUZZY;
	case IMPLIED:
	case IMPLIED_MIN:
	case IMPLIED_MAX:
		return RL_IMPLIED;
	case ABSOLUTE_MIN:
	case ABSOLUTE_MAX:
		return RL_ABSOLUTE;
	}
	return 0;
}

static int opposite_implied(int implied)
{
	if (implied == IMPLIED_MIN)
		return IMPLIED_MAX;
	if (implied == IMPLIED_MAX)
		return IMPLIED_MIN;
	if (implied == FUZZY_MIN)
		return FUZZY_MAX;
	if (implied == FUZZY_MAX)
		return FUZZY_MIN;
	if (implied == ABSOLUTE_MIN)
		return ABSOLUTE_MAX;
	if (implied == ABSOLUTE_MAX)
		return ABSOLUTE_MIN;
	if (implied == HARD_MAX)  /* we don't have a hard min.  */
		return EXACT;

	return implied;
}

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

static int last_stmt_sval(struct statement *stmt, sval_t *sval)
{
	struct expression *expr;

	if (!stmt)
		return 0;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return 0;
	expr = stmt->expression;
	if (!get_value(expr, sval))
		return 0;
	return 1;
}

static sval_t handle_expression_statement(struct expression *expr, int *undefined, int implied)
{
	struct statement *stmt;
	sval_t ret;

	stmt = get_expression_statement(expr);
	if (!last_stmt_sval(stmt, &ret)) {
		*undefined = 1;
		ret = bogus;
	}

	return ret;
}

static struct range_list *handle_ampersand_rl(int implied)
{
	if (implied == EXACT || implied == HARD_MAX)
		return NULL;
	return alloc_rl(valid_ptr_min_sval, valid_ptr_max_sval);
}

static sval_t handle_ampersand(int *undefined, int implied)
{
	sval_t ret;

	ret.type = &ptr_ctype;
	ret.value = BOGUS;

	if (implied == IMPLIED_MIN || implied == FUZZY_MIN || implied == ABSOLUTE_MIN)
		return valid_ptr_min_sval;
	if (implied == IMPLIED_MAX || implied == FUZZY_MAX || implied == ABSOLUTE_MAX)
		return valid_ptr_max_sval;

	*undefined = 1;
	return ret;
}

static struct range_list *handle_negate_rl(struct expression *expr, int implied)
{
	if (known_condition_true(expr->unop))
		return rl_zero();
	if (known_condition_false(expr->unop))
		return rl_one();

	if (implied == EXACT)
		return NULL;

	if (implied_condition_true(expr->unop))
		return rl_zero();
	if (implied_condition_false(expr->unop))
		return rl_one();
	return alloc_rl(zero, one);
}

static sval_t handle_negate(struct expression *expr, int *undefined, int implied)
{
	sval_t ret;

	ret = sval_blank(expr->unop);

	if (known_condition_true(expr->unop)) {
		ret.value = 0;
		return ret;
	}

	if (implied == EXACT) {
		*undefined = 1;
		return bogus;
	}

	if (implied_condition_true(expr->unop)) {
		ret.value = 0;
		return ret;
	}
	if (implied_condition_false(expr->unop)) {
		ret.value = 1;
		return ret;
	}
	if (implied == IMPLIED_MIN || implied == FUZZY_MIN || implied == ABSOLUTE_MIN) {
		ret.value = 0;
		return ret;
	}
	if (implied == IMPLIED_MAX || implied == FUZZY_MAX || implied == ABSOLUTE_MAX) {
		ret.value = 1;
		return ret;
	}
	*undefined = 1;
	return bogus;
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

static sval_t handle_preop(struct expression *expr, int *undefined, int implied)
{
	sval_t ret;

	switch (expr->op) {
	case '&':
		ret = handle_ampersand(undefined, implied);
		break;
	case '!':
		ret = handle_negate(expr, undefined, implied);
		break;
	case '~':
		ret = _get_value(expr->unop, undefined, implied);
		ret = sval_preop(ret, '~');
		ret = sval_cast(get_type(expr->unop), ret);
		break;
	case '-':
		ret = _get_value(expr->unop, undefined, implied);
		ret = sval_preop(ret, '-');
		break;
	case '*':
		ret = _get_implied_value(expr, undefined, implied);
		break;
	case '(':
		ret = handle_expression_statement(expr, undefined, implied);
		break;
	default:
		*undefined = 1;
		ret = sval_blank(expr);
	}
	ret = sval_cast(get_type(expr), ret);
	return ret;
}

static sval_t handle_divide(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, opposite_implied(implied));

	if (right.value == 0) {
		*undefined = 1;
		return bogus;
	}

	return sval_binop(left, '/', right);
}

static sval_t handle_subtract(struct expression *expr, int *undefined, int implied)
{
	struct symbol *type;
	sval_t left, right, ret;
	int left_undefined = 0;
	int right_undefined = 0;
	int known_but_negative = 0;
	int comparison;

	left = _get_value(expr->left, &left_undefined, implied);
	right = _get_value(expr->right, &right_undefined, opposite_implied(implied));

	if (!left_undefined && !right_undefined) {
		ret = sval_binop(left, '-', right);
		if (sval_is_negative(ret))
			known_but_negative = 1;
		else
			return ret;  /* best case scenario */
	}

	comparison = get_comparison(expr->left, expr->right);
	if (!comparison)
		goto bogus;

	type = get_type(expr);

	switch (comparison) {
	case '>':
	case SPECIAL_UNSIGNED_GT:
		switch (implied) {
		case IMPLIED_MIN:
		case FUZZY_MIN:
		case ABSOLUTE_MIN:
			return sval_type_val(type, 1);
		case IMPLIED_MAX:
		case FUZZY_MAX:
		case ABSOLUTE_MAX:
			return _get_value(expr->left, undefined, implied);
		}
		break;
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GTE:
		switch (implied) {
		case IMPLIED_MIN:
		case FUZZY_MIN:
		case ABSOLUTE_MIN:
			return sval_type_val(type, 0);
		case IMPLIED_MAX:
		case FUZZY_MAX:
		case ABSOLUTE_MAX:
			return _get_value(expr->left, undefined, implied);
		}
		break;
	}

	if (known_but_negative)
		return ret;

bogus:
	*undefined = 1;
	return bogus;
}

static sval_t handle_mod(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;

	/* if we can't figure out the right side it's probably hopeless */
	right = _get_value(expr->right, undefined, implied);
	if (*undefined || right.value == 0) {
		*undefined = 1;
		return bogus;
	}

	switch (implied) {
	case EXACT:
	case IMPLIED:
		left = _get_value(expr->left, undefined, implied);
		if (!*undefined)
			return sval_binop(left, '%', right);
		return bogus;
	case IMPLIED_MIN:
	case FUZZY_MIN:
	case ABSOLUTE_MIN:
		*undefined = 0;
		return sval_type_val(get_type(expr), 0);
	case IMPLIED_MAX:
	case FUZZY_MAX:
	case ABSOLUTE_MAX:
		*undefined = 0;
		right = sval_cast(get_type(expr), right);
		right.value--;
		return right;
	}
	return bogus;
}

static struct range_list *handle_binop_rl(struct expression *expr, int implied)
{
	struct symbol *type;
	sval_t left, right;
	sval_t ret = {.type = &int_ctype, {.value = 123456} };
	int local_undef = 0;
	int undefined = 0;

	switch (expr->op) {
	case '%':
		ret = handle_mod(expr, &undefined, implied);
		if (undefined)
			return NULL;
		return alloc_rl(ret, ret);
	case '&':
		if (implied == HARD_MAX)
			return NULL;
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == IMPLIED_MIN || implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return alloc_rl(ret, ret);
			}
			if (implied != IMPLIED_MAX && implied != ABSOLUTE_MAX)
				undefined = 1;
			if (!get_absolute_max(expr->left, &left))
				undefined = 1;
		}
		right = _get_value(expr->right, &undefined, implied);
		if (undefined)
			return NULL;
		ret = sval_binop(left, '&', right);
		return alloc_rl(ret, ret);

	case SPECIAL_RIGHTSHIFT:
		if (implied == HARD_MAX)
			return NULL;
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == IMPLIED_MIN || implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return alloc_rl(ret, ret);
			}
			if (implied != IMPLIED_MAX && implied != ABSOLUTE_MAX)
				undefined = 1;
			if (!get_absolute_max(expr->left, &left))
				undefined = 1;
		}
		right = _get_value(expr->right, &undefined, implied);
		if (undefined)
			return NULL;
		ret = sval_binop(left, SPECIAL_RIGHTSHIFT, right);
		return alloc_rl(ret, ret);
	case '-':
		ret = handle_subtract(expr, &undefined, implied);
		if (undefined)
			return NULL;
		return alloc_rl(ret, ret);
	}

	left = _get_value(expr->left, &undefined, implied);
	right = _get_value(expr->right, &undefined, implied);

	if (undefined)
		return NULL;

	type = get_type(expr);
	left = sval_cast(type, left);
	right = sval_cast(type, right);

	switch (implied) {
	case IMPLIED_MAX:
	case ABSOLUTE_MAX:
		if (sval_binop_overflows(left, expr->op, right)) {
			ret = sval_type_max(get_type(expr));
			return alloc_rl(ret, ret);
		}
		break;
	case HARD_MAX:
	case FUZZY_MAX:
		if (sval_binop_overflows(left, expr->op, right))
			return NULL;
	}

	switch (expr->op) {
	case '/':
		ret = handle_divide(expr, &undefined, implied);
		if (undefined)
			return NULL;
		return alloc_rl(ret, ret);
	case '%':
		if (right.value == 0) {
			return NULL;
		} else {
			ret = sval_binop(left, '%', right);
			return alloc_rl(ret, ret);
		}
	default:
		ret = sval_binop(left, expr->op, right);
	}
	return alloc_rl(ret, ret);
}

static sval_t handle_binop(struct expression *expr, int *undefined, int implied)
{
	struct symbol *type;
	sval_t left, right;
	sval_t ret = {.type = &int_ctype, {.value = 123456} };
	int local_undef = 0;

	switch (expr->op) {
	case '%':
		return handle_mod(expr, undefined, implied);
	case '&':
		if (implied == HARD_MAX) {
			*undefined = 1;
			return bogus;
		}
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == IMPLIED_MIN || implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return ret;
			}
			if (implied != IMPLIED_MAX && implied != ABSOLUTE_MAX)
				*undefined = 1;
			if (!get_absolute_max(expr->left, &left))
				*undefined = 1;
		}
		right = _get_value(expr->right, undefined, implied);
		if (*undefined)
			return bogus;
		return sval_binop(left, '&', right);

	case SPECIAL_RIGHTSHIFT:
		if (implied == HARD_MAX) {
			*undefined = 1;
			return bogus;
		}
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == IMPLIED_MIN || implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return ret;
			}
			if (implied != IMPLIED_MAX && implied != ABSOLUTE_MAX)
				*undefined = 1;
			if (!get_absolute_max(expr->left, &left))
				*undefined = 1;
		}
		right = _get_value(expr->right, undefined, implied);
		if (*undefined)
			return bogus;
		return sval_binop(left, SPECIAL_RIGHTSHIFT, right);
	case '-':
		return handle_subtract(expr, undefined, implied);
	}

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, implied);

	if (*undefined)
		return bogus;

	type = get_type(expr);
	left = sval_cast(type, left);
	right = sval_cast(type, right);

	switch (implied) {
	case IMPLIED_MAX:
	case ABSOLUTE_MAX:
		if (sval_binop_overflows(left, expr->op, right))
			return sval_type_max(get_type(expr));
		break;
	case HARD_MAX:
	case FUZZY_MAX:
		if (sval_binop_overflows(left, expr->op, right)) {
			*undefined = 1;
			return bogus;
		}
	}

	switch (expr->op) {
	case '/':
		return handle_divide(expr, undefined, implied);
	case '%':
		if (right.value == 0) {
			*undefined = 1;
			return bogus;
		} else {
			return sval_binop(left, '%', right);
		}
	default:
		ret = sval_binop(left, expr->op, right);
	}
	return ret;
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

	if (implied == EXACT)
		return NULL;

	res = do_comparison(expr);
	if (res == 1)
		return rl_one();
	if (res == 2)
		return rl_zero();

	return alloc_rl(zero, one);
}

static sval_t handle_comparison(struct expression *expr, int *undefined, int implied)
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
			return one;
		return zero;
	}

	if (implied == EXACT) {
		*undefined = 1;
		return bogus;
	}

	res = do_comparison(expr);
	if (res == 1)
		return one;
	if (res == 2)
		return zero;

	if (implied == IMPLIED_MIN || implied == FUZZY_MIN || implied == ABSOLUTE_MIN)
		return zero;
	if (implied == IMPLIED_MAX || implied == FUZZY_MAX || implied == ABSOLUTE_MAX)
		return one;

	*undefined = 1;
	return bogus;
}

static struct range_list *handle_logical_rl(struct expression *expr, int implied)
{
	sval_t left, right;
	int left_known = 0;
	int right_known = 0;

	if (implied == EXACT) {
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

	if (implied == EXACT)
		return NULL;

	return alloc_rl(zero, one);
}

static sval_t handle_logical(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;
	int left_known = 0;
	int right_known = 0;

	if (implied == EXACT) {
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
			return one;
		if (right_known && right.value)
			return one;
		if (left_known && right_known)
			return zero;
		break;
	case SPECIAL_LOGICAL_AND:
		if (left_known && right_known) {
			if (left.value && right.value)
				return one;
			return zero;
		}
		break;
	default:
		*undefined = 1;
		return bogus;
	}

	if (implied == IMPLIED_MIN || implied == FUZZY_MIN || implied == ABSOLUTE_MIN)
		return zero;
	if (implied == IMPLIED_MAX || implied == FUZZY_MAX || implied == ABSOLUTE_MAX)
		return one;

	*undefined = 1;
	return bogus;
}

static struct range_list *handle_conditional_rl(struct expression *expr, int implied)
{
	struct range_list *true_rl, *false_rl;

	if (known_condition_true(expr->conditional))
		return _get_rl(expr->cond_true, implied);
	if (known_condition_false(expr->conditional))
		return _get_rl(expr->cond_false, implied);

	if (implied == EXACT)
		return NULL;

	if (implied_condition_true(expr->conditional))
		return _get_rl(expr->cond_true, implied);
	if (implied_condition_false(expr->conditional))
		return _get_rl(expr->cond_false, implied);

	true_rl = _get_rl(expr->cond_true, implied);
	if (!true_rl)
		return NULL;
	false_rl = _get_rl(expr->cond_false, implied);
	if (!false_rl)
		return NULL;

	return rl_union(true_rl, false_rl);
}

static sval_t handle_conditional(struct expression *expr, int *undefined, int implied)
{
	if (known_condition_true(expr->conditional))
		return _get_value(expr->cond_true, undefined, implied);
	if (known_condition_false(expr->conditional))
		return _get_value(expr->cond_false, undefined, implied);

	if (implied == EXACT) {
		*undefined = 1;
		return bogus;
	}

	if (implied_condition_true(expr->conditional))
		return _get_value(expr->cond_true, undefined, implied);
	if (implied_condition_false(expr->conditional))
		return _get_value(expr->cond_false, undefined, implied);

	*undefined = 1;
	return bogus;
}

static int get_local_value(struct expression *expr, sval_t *sval, int implied)
{
	switch (implied) {
	case EXACT:
	case IMPLIED:
		return 0;
	case IMPLIED_MIN:
	case FUZZY_MIN:
	case ABSOLUTE_MIN:
		return get_local_min_helper(expr, sval);
	case ABSOLUTE_MAX:
	case HARD_MAX:
	case IMPLIED_MAX:
	case FUZZY_MAX:
		return get_local_max_helper(expr, sval);
	}
	return 0;
}

static int get_implied_value_helper(struct expression *expr, sval_t *sval, int implied)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	/* fixme: this should return the casted value */

	expr = strip_expr(expr);

	if (get_value(expr, sval))
		return 1;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return 0;
	*sval = sval_blank(expr);
	state = get_state(SMATCH_EXTRA, name, sym);
	free_string(name);
	if (!state || !state->data)
		return get_local_value(expr, sval, implied);
	if (implied == IMPLIED) {
		if (estate_get_single_value(state, sval))
			return 1;
		return 0;
	}
	if (implied == HARD_MAX) {
		if (estate_get_hard_max(state, sval))
			return 1;
		return 0;
	}
	if (implied == IMPLIED_MAX || implied == ABSOLUTE_MAX) {
		*sval = estate_max(state);
		return 1;
	}
	*sval = estate_min(state);
	return 1;
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

	switch (implied_to_rl_enum(implied)) {
	case RL_EXACT:
		return NULL;
	case RL_HARD:
	case RL_IMPLIED:
	case RL_ABSOLUTE:
		state = get_state_expr(SMATCH_EXTRA, expr);
		if (!state || !state->data) {
			if (get_local_rl(expr, &rl))
				return rl;
			return NULL;
		}
		if (implied == HARD_MAX && !estate_has_hard_max(state))
			return NULL;
		return estate_rl(state);
	case RL_FUZZY:
		if (!get_fuzzy_min_helper(expr, &min))
			min = sval_type_min(get_type(expr));
		if (!get_fuzzy_max_helper(expr, &max))
			return NULL;
		return alloc_rl(min, max);
	}
	return NULL;
}

static sval_t _get_implied_value(struct expression *expr, int *undefined, int implied)
{
	sval_t ret;

	ret = sval_blank(expr);

	switch (implied) {
	case IMPLIED:
	case IMPLIED_MAX:
	case IMPLIED_MIN:
	case HARD_MAX:
	case ABSOLUTE_MIN:
	case ABSOLUTE_MAX:
		if (!get_implied_value_helper(expr, &ret, implied))
			*undefined = 1;
		break;
	case FUZZY_MAX:
		if (!get_fuzzy_max_helper(expr, &ret))
			*undefined = 1;
		break;
	case FUZZY_MIN:
		if (!get_fuzzy_min_helper(expr, &ret))
			*undefined = 1;
		break;
	default:
		*undefined = 1;
	}
	return ret;
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

	if (implied == EXACT)
		return NULL;

	if (get_implied_return(expr, &rl))
		return rl;
	return db_return_vals(expr);
}

static sval_t handle_call(struct expression *expr, int *undefined, int implied)
{
	struct range_list *rl;

	if (!get_implied_rl(expr, &rl)) {
		*undefined = 1;
		return bogus;
	}

	switch (implied) {
	case IMPLIED:
		if (sval_cmp(rl_min(rl), rl_max(rl)) == 0)
			return rl_min(rl);
		*undefined = 1;
		return bogus;
	case IMPLIED_MIN:
	case ABSOLUTE_MIN:
		return rl_min(rl);

	case IMPLIED_MAX:
	case HARD_MAX:
	case ABSOLUTE_MAX:
		return rl_max(rl);
	default:
		*undefined = 1;
		return bogus;
	}

}

static struct range_list *_get_rl(struct expression *expr, int implied)
{
	struct range_list *rl;
	struct symbol *type;
	int undefined;
	sval_t sval;

	expr = strip_parens(expr);
	if (!expr)
		return NULL;
	type = get_type(expr);

	undefined = 0;
	switch (expr->type) {
	case EXPR_VALUE:
		sval = sval_from_val(expr, expr->value);
		break;
	case EXPR_PREOP:
		rl = handle_preop_rl(expr, implied);
		if (rl)
			return rl;
		undefined = 1;
		break;
	case EXPR_POSTOP:
		return _get_rl(expr->unop, implied);
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		rl = _get_rl(expr->cast_expression, implied);
		if (!rl)
			undefined = 1;
		else
			return cast_rl(type, rl);
		break;
	case EXPR_BINOP:
		rl = handle_binop_rl(expr, implied);
		if (rl)
			return rl;
		undefined = 1;
		break;
	case EXPR_COMPARE:
		rl = handle_comparison_rl(expr, implied);
		if (rl)
			return rl;
		undefined = 1;
		break;
	case EXPR_LOGICAL:
		rl = handle_logical_rl(expr, implied);
		if (rl)
			return rl;
		undefined = 1;
		break;
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		sval = handle_sizeof(expr);
		break;
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		rl = handle_conditional_rl(expr, implied);
		if (rl)
			return rl;
		undefined = 1;
		break;
	case EXPR_CALL:
		rl = handle_call_rl(expr, implied);
		if (rl)
			return rl;
		else
			undefined = 1;
		break;
	default:
		rl = handle_variable(expr, implied);
		if (rl)
			return rl;
		else
			undefined = 1;
	}

	if (undefined && type &&
	    (implied == ABSOLUTE_MAX || implied == ABSOLUTE_MIN))
		return alloc_whole_rl(type);
	if (undefined)
		return NULL;
	rl = alloc_rl(sval, sval);
	return rl;
}

static sval_t _get_value(struct expression *expr, int *undefined, int implied)
{
	struct symbol *type;
	sval_t ret;

	if (!expr) {
		*undefined = 1;
		return bogus;
	}
	if (*undefined)
		return bogus;

	expr = strip_parens(expr);
	type = get_type(expr);

	switch (expr->type) {
	case EXPR_VALUE:
		ret = sval_from_val(expr, expr->value);
		break;
	case EXPR_PREOP:
		ret = handle_preop(expr, undefined, implied);
		break;
	case EXPR_POSTOP:
		ret = _get_value(expr->unop, undefined, implied);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		ret = _get_value(expr->cast_expression, undefined, implied);
		ret = sval_cast(type, ret);
		break;
	case EXPR_BINOP:
		ret = handle_binop(expr, undefined, implied);
		break;
	case EXPR_COMPARE:
		ret = handle_comparison(expr, undefined, implied);
		break;
	case EXPR_LOGICAL:
		ret = handle_logical(expr, undefined, implied);
		break;
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		ret = handle_sizeof(expr);
		break;
	case EXPR_SYMBOL:
		if (get_const_value(expr, &ret)) {
			break;
		}
		ret = _get_implied_value(expr, undefined, implied);
		break;
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		ret = handle_conditional(expr, undefined, implied);
		break;
	case EXPR_CALL:
		ret = handle_call(expr, undefined, implied);
		break;
	default:
		ret = _get_implied_value(expr, undefined, implied);
	}

	if (*undefined)
		return bogus;
	return ret;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl = _get_rl(expr, EXACT);
	if (!rl_to_sval(rl, sval))
		return 0;
	return 1;
}

int get_implied_value(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, IMPLIED);
	if (!rl_to_sval(rl, sval))
		return 0;
	return 1;
}

int get_implied_min(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, IMPLIED_MIN);
	if (!rl)
		return 0;
	*sval = rl_min(rl);
	return 1;
}

int get_implied_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, IMPLIED_MAX);
	if (!rl)
		return 0;
	*sval = rl_max(rl);
	return 1;
}

int get_implied_rl(struct expression *expr, struct range_list **rl)
{
	sval_t sval;
	struct smatch_state *state;
	sval_t min, max;
	int min_known = 0;
	int max_known = 0;

	*rl = NULL;

	expr = strip_parens(expr);
	if (!expr)
		return 0;

	state = get_state_expr(SMATCH_EXTRA, expr);
	if (state) {
		*rl = clone_rl(estate_rl(state));
		return 1;
	}

	if (expr->type == EXPR_CALL) {
		if (get_implied_return(expr, rl))
			return 1;
		*rl = db_return_vals(expr);
		if (*rl)
			return 1;
		return 0;
	}

	if (get_implied_value(expr, &sval)) {
		add_range(rl, sval, sval);
		return 1;
	}

	if (get_local_rl(expr, rl))
		return 1;

	min_known = get_implied_min(expr, &min);
	max_known = get_implied_max(expr, &max);
	if (!min_known && !max_known)
		return 0;
	if (!min_known)
		get_absolute_min(expr, &min);
	if (!max_known)
		get_absolute_max(expr, &max);

	*rl = alloc_rl(min, max);
	return 1;
}

int get_hard_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, HARD_MAX);
	if (!rl)
		return 0;
	*sval = rl_max(rl);
	return 1;
}

int get_fuzzy_min(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	rl =  _get_rl(expr, FUZZY_MIN);
	if (!rl)
		return 0;
	*sval = rl_min(rl);
	return 1;
}

int get_fuzzy_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;
	sval_t max;

	rl =  _get_rl(expr, FUZZY_MAX);
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
	rl = _get_rl(expr, ABSOLUTE_MIN);
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
	rl = _get_rl(expr, ABSOLUTE_MAX);
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
