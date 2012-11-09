/*
 * smatch/smatch_math.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static sval_t _get_value(struct expression *expr, int *undefined, int implied);
static sval_t _get_implied_value(struct expression *expr, int *undefined, int implied);

#define BOGUS 12345

static sval_t zero  = {.type = &int_ctype, .value = 0};
static sval_t one   = {.type = &int_ctype, .value = 1};
static sval_t bogus = {.type = &int_ctype, .value = BOGUS};

#define NOTIMPLIED  0
#define IMPLIED     1
#define IMPLIED_MIN 2
#define IMPLIED_MAX 3
#define FUZZYMAX    4
#define FUZZYMIN    5
#define ABSOLUTE_MIN 6
#define ABSOLUTE_MAX 7

static int opposite_implied(int implied)
{
	if (implied == IMPLIED_MIN)
		return IMPLIED_MAX;
	if (implied == IMPLIED_MAX)
		return IMPLIED_MIN;
	if (implied == ABSOLUTE_MIN)
		return ABSOLUTE_MAX;
	if (implied == ABSOLUTE_MAX)
		return ABSOLUTE_MIN;
	return implied;
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
	if (!get_value_sval(expr, sval))
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
		ret.value = BOGUS;
	}

	return ret;
}

static sval_t handle_ampersand(int *undefined, int implied)
{
	sval_t ret;

	ret.type = &ptr_ctype;
	ret.value = BOGUS;

	if (implied == IMPLIED_MIN || implied == FUZZYMIN || implied == ABSOLUTE_MIN)
		return valid_ptr_min_sval;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX || implied == ABSOLUTE_MAX)
		return valid_ptr_max_sval;

	*undefined = 1;
	return ret;
}

static sval_t handle_preop(struct expression *expr, int *undefined, int implied)
{
	sval_t ret;

	switch (expr->op) {
	case '&':
		ret = handle_ampersand(undefined, implied);
		break;
	case '!':
		ret = _get_value(expr->unop, undefined, implied);
		ret = sval_preop(ret, '!');
		break;
	case '~':
		ret = _get_value(expr->unop, undefined, implied);
		ret = sval_preop(ret, '~');
		ret = sval_cast(ret, get_type(expr->unop));
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
	ret = sval_cast(ret, get_type(expr));
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
	sval_t left, right;

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, opposite_implied(implied));

	return sval_binop(left, '-', right);
}

static sval_t handle_binop(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;
	sval_t ret = {.type = &int_ctype, .value = 123456};
	int local_undef = 0;

	switch (expr->op) {
	case '%':
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return ret;
			}
			if (implied != ABSOLUTE_MAX)
				*undefined = 1;
			if (!get_absolute_max_sval(expr->left, &left))
				*undefined = 1;
		}
		right = _get_value(expr->right, undefined, implied);
		if (right.value == 0)
			*undefined = 1;
		else
			ret = sval_binop(left, '%', right);
		return ret;

	case '&':
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return ret;
			}
			if (implied != ABSOLUTE_MAX)
				*undefined = 1;
			if (!get_absolute_max_sval(expr->left, &left))
				*undefined = 1;
		}
		right = _get_value(expr->right, undefined, implied);
		return sval_binop(left, '&', right);

	case SPECIAL_RIGHTSHIFT:
		left = _get_value(expr->left, &local_undef, implied);
		if (local_undef) {
			if (implied == ABSOLUTE_MIN) {
				ret = sval_blank(expr->left);
				ret.value = 0;
				return ret;
			}
			if (implied != ABSOLUTE_MAX)
				*undefined = 1;
			if (!get_absolute_max_sval(expr->left, &left))
				*undefined = 1;
		}
		right = _get_value(expr->right, undefined, implied);
		return sval_binop(left, SPECIAL_RIGHTSHIFT, right);
	}

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, implied);

	switch (expr->op) {
	case '/':
		return handle_divide(expr, undefined, implied);
	case '%':
		if (right.value == 0)
			*undefined = 1;
		else
			ret = sval_binop(left, '%', right);
		break;
	case '-':
		ret = handle_subtract(expr, undefined, implied);
		break;
	default:
		ret = sval_binop(left, expr->op, right);
	}
	return ret;
}

static int do_comparison(struct expression *expr)
{
	struct range_list_sval *left_ranges = NULL;
	struct range_list_sval *right_ranges = NULL;
	int poss_true, poss_false;

	get_implied_range_list_sval(expr->left, &left_ranges);
	get_implied_range_list_sval(expr->right, &right_ranges);

	poss_true = possibly_true_range_lists_sval(left_ranges, expr->op, right_ranges);
	poss_false = possibly_false_range_lists_sval(left_ranges, expr->op, right_ranges);

	free_range_list_sval(&left_ranges);
	free_range_list_sval(&right_ranges);

	if (!poss_true && !poss_false)
		return 0;
	if (poss_true && !poss_false)
		return 1;
	if (!poss_true && poss_false)
		return 2;
	return 3;
}

static sval_t handle_comparison(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;
	int res;

	if (get_value_sval(expr->left, &left) && get_value_sval(expr->right, &right)) {
		struct data_range_sval tmp_left, tmp_right;

		tmp_left.min = left;
		tmp_left.max = left;
		tmp_right.min = right;
		tmp_right.max = right;
		if (true_comparison_range_sval(&tmp_left, expr->op, &tmp_right))
			return one;
		return zero;
	}

	if (implied == NOTIMPLIED) {
		*undefined = 1;
		return bogus;
	}

	res = do_comparison(expr);
	if (res == 1)
		return one;
	if (res == 2)
		return zero;

	if (implied == IMPLIED_MIN || implied == FUZZYMIN || implied == ABSOLUTE_MIN)
		return zero;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX || implied == ABSOLUTE_MAX)
		return one;

	*undefined = 1;
	return bogus;
}

static sval_t handle_logical(struct expression *expr, int *undefined, int implied)
{
	sval_t left, right;

	if ((implied == NOTIMPLIED && get_value_sval(expr->left, &left) &&
				      get_value_sval(expr->right, &right)) ||
	    (implied != NOTIMPLIED && get_implied_value_sval(expr->left, &left) &&
				      get_implied_value_sval(expr->right, &right))) {
		switch (expr->op) {
		case SPECIAL_LOGICAL_OR:
			if (left.value || right.value)
				return one;
			return zero;
		case SPECIAL_LOGICAL_AND:
			if (left.value && right.value)
				return one;
			return zero;
		default:
			*undefined = 1;
			return bogus;
		}
	}

	if (implied == IMPLIED_MIN || implied == FUZZYMIN || implied == ABSOLUTE_MIN)
		return zero;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX || implied == ABSOLUTE_MAX)
		return one;

	*undefined = 1;
	return bogus;
}

static sval_t handle_conditional(struct expression *expr, int *undefined, int implied)
{
	if (known_condition_true(expr->conditional))
		return _get_value(expr->cond_true, undefined, implied);
	if (known_condition_false(expr->conditional))
		return _get_value(expr->cond_false, undefined, implied);

	if (implied == NOTIMPLIED) {
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

static int get_implied_value_helper(struct expression *expr, sval_t *sval, int implied)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	/* fixme: this should return the casted value */

	expr = strip_expr(expr);

	if (get_value_sval(expr, sval))
		return 1;

	name = get_variable_from_expr(expr, &sym);
	if (!name)
		return 0;
	*sval = sval_blank(expr);
	state = get_state(SMATCH_EXTRA, name, sym);
	free_string(name);
	if (!state || !state->data)
		return 0;
	if (implied == IMPLIED) {
		if (estate_get_single_value_sval(state, sval))
			return 1;
		return 0;
	}
	if (implied == IMPLIED_MAX || implied == ABSOLUTE_MAX) {
		*sval = estate_max_sval(state);
		if (sval_is_max(*sval)) /* this means just guessing.  fixme. not really */
			return 0;
		return 1;
	}
	*sval = estate_min_sval(state);
	if (sval_is_min(*sval))       /* fixme */
		return 0;
	return 1;
}

static int get_fuzzy_max_helper(struct expression *expr, sval_t *max)
{
	struct sm_state *sm;
	struct sm_state *tmp;
	sval_t sval;

	if (get_implied_max_sval(expr, max))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	sval = sval_type_min(&llong_ctype);
	FOR_EACH_PTR(sm->possible, tmp) {
		sval_t new_min;

		new_min = estate_min_sval(tmp->state);
		if (sval_cmp(new_min, sval) > 0)
			sval = new_min;
	} END_FOR_EACH_PTR(tmp);

	if (sval_is_min(sval))
		return 0;

	*max = sval_cast(sval, get_type(expr));
	return 1;
}

static int get_fuzzy_min_helper(struct expression *expr, sval_t *min)
{
	struct sm_state *sm;
	struct sm_state *tmp;
	sval_t sval;

	if (get_implied_min_sval(expr, min))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	sval = sval_type_max(&llong_ctype);
	FOR_EACH_PTR(sm->possible, tmp) {
		sval_t new_max;

		new_max = estate_max_sval(tmp->state);
		if (sval_cmp(new_max, sval) < 0)
			sval = new_max;
	} END_FOR_EACH_PTR(tmp);

	if (sval_is_max(sval))
		return 0;
	*min = sval_cast(sval, get_type(expr));
	return 1;
}

static sval_t _get_implied_value(struct expression *expr, int *undefined, int implied)
{
	sval_t ret;

	ret = sval_blank(expr);

	switch (implied) {
	case IMPLIED:
	case IMPLIED_MAX:
	case IMPLIED_MIN:
		if (!get_implied_value_helper(expr, &ret, implied))
			*undefined = 1;
		break;
	case ABSOLUTE_MIN:
		if (!get_absolute_min_helper(expr, &ret))
			*undefined = 1;
		break;
	case ABSOLUTE_MAX:
		if (!get_absolute_max_helper(expr, &ret))
			*undefined = 1;
		break;
	case FUZZYMAX:
		if (!get_fuzzy_max_helper(expr, &ret))
			*undefined = 1;
		break;
	case FUZZYMIN:
		if (!get_fuzzy_min_helper(expr, &ret))
			*undefined = 1;
		break;
	default:
		*undefined = 1;
	}
	return ret;
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
	if (get_value_sval(sym->initializer, &right)) {
		*sval = sval_cast(right, get_type(expr));
		return 1;
	}
	return 0;
}

static sval_t _get_value(struct expression *expr, int *undefined, int implied)
{
	sval_t tmp_ret;

	if (!expr) {
		*undefined = 1;
		return bogus;
	}
	if (*undefined)
		return bogus;

	expr = strip_parens(expr);

	switch (expr->type) {
	case EXPR_VALUE:
		tmp_ret = sval_from_val(expr, expr->value);
		break;
	case EXPR_PREOP:
		tmp_ret = handle_preop(expr, undefined, implied);
		break;
	case EXPR_POSTOP:
		tmp_ret = _get_value(expr->unop, undefined, implied);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		tmp_ret = _get_value(expr->cast_expression, undefined, implied);
		tmp_ret = sval_cast(tmp_ret, get_type(expr));
		break;
	case EXPR_BINOP:
		tmp_ret = handle_binop(expr, undefined, implied);
		break;
	case EXPR_COMPARE:
		tmp_ret = handle_comparison(expr, undefined, implied);
		break;
	case EXPR_LOGICAL:
		tmp_ret = handle_logical(expr, undefined, implied);
		break;
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		tmp_ret = sval_blank(expr);
		tmp_ret.value = get_expression_value_nomod(expr);
		break;
	case EXPR_SYMBOL:
		if (get_const_value(expr, &tmp_ret)) {
			break;
		}
		tmp_ret = _get_implied_value(expr, undefined, implied);
		break;
	case EXPR_SELECT:
	case EXPR_CONDITIONAL:
		tmp_ret = handle_conditional(expr, undefined, implied);
		break;
	default:
		tmp_ret = _get_implied_value(expr, undefined, implied);
	}
	if (*undefined)
		return bogus;
	return tmp_ret;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret = _get_value(expr, &undefined, NOTIMPLIED);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_implied_value_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret =  _get_value(expr, &undefined, IMPLIED);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_implied_min_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret =  _get_value(expr, &undefined, IMPLIED_MIN);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_implied_max_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret =  _get_value(expr, &undefined, IMPLIED_MAX);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_fuzzy_min_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret =  _get_value(expr, &undefined, FUZZYMIN);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_fuzzy_max_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	sval_t ret;

	ret =  _get_value(expr, &undefined, FUZZYMAX);
	if (undefined)
		return 0;
	*sval = ret;
	return 1;
}

int get_absolute_min_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	struct symbol *type;

	type = get_type(expr);
	*sval =  _get_value(expr, &undefined, ABSOLUTE_MIN);
	if (undefined) {
		*sval = sval_type_min(type);
		return 1;
	}

	if (sval_cmp(*sval, sval_type_min(type)) < 0)
		*sval = sval_type_min(type);
	return 1;
}

int get_absolute_max_sval(struct expression *expr, sval_t *sval)
{
	int undefined = 0;
	struct symbol *type;

	type = get_type(expr);
	*sval = _get_value(expr, &undefined, ABSOLUTE_MAX);
	if (undefined) {
		*sval = sval_type_max(type);
		return 1;
	}

	if (sval_cmp(sval_type_max(type), *sval) < 0)
		*sval = sval_type_max(type);
	return 1;
}

int known_condition_true(struct expression *expr)
{
	sval_t tmp;

	if (!expr)
		return 0;

	if (get_value_sval(expr, &tmp) && tmp.value)
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
	if (get_implied_value_sval(expr, &tmp) && tmp.value)
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
		if (get_implied_value_sval(expr, &sval) && sval.value == 0)
			return 1;
		break;
	}
	return 0;
}
