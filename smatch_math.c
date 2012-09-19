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

static long long _get_implied_value(struct expression *expr, int *undefined, int implied);
static long long _get_value(struct expression *expr, int *undefined, int implied);

#define BOGUS 12345

#define NOTIMPLIED  0
#define IMPLIED     1
#define IMPLIED_MIN 2
#define IMPLIED_MAX 3
#define FUZZYMAX    4
#define FUZZYMIN    5

static long long cast_to_type(struct expression *expr, long long val)
{
	struct symbol *type = get_type(expr);

	if (!type)
		return val;

	switch (type->bit_size) {
	case 8:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned char) val;
		else
			val = (long long)(char) val;
		break;
	case 16:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned short) val;
		else
			val = (long long)(short) val;
		break;
	case 32:
		if (type->ctype.modifiers & MOD_UNSIGNED)
			val = (long long)(unsigned int) val;
		else
			val = (long long)(int) val;
		break;
	}
	return val;
}

static int opposite_implied(int implied)
{
	if (implied == IMPLIED_MIN)
		return IMPLIED_MAX;
	if (implied == IMPLIED_MAX)
		return IMPLIED_MIN;
	return implied;
}

static int last_stmt_val(struct statement *stmt, long long *val)
{
	struct expression *expr;

	if (!stmt)
		return 0;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return 0;
	expr = stmt->expression;
	return get_value(expr, val);
}

static long long handle_expression_statement(struct expression *expr, int *undefined, int implied)
{
	struct statement *stmt;
	long long tmp;

	stmt = get_expression_statement(expr);
	if (last_stmt_val(stmt, &tmp))
		return tmp;

	*undefined = 1;
	return BOGUS;
}

static long long handle_ampersand(int *undefined, int implied)
{
	if (implied == IMPLIED_MIN || implied == FUZZYMIN)
		return valid_ptr_min;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX)
		return valid_ptr_max;

	*undefined = 1;
	return BOGUS;
}

static long long handle_preop(struct expression *expr, int *undefined, int implied)
{
	long long ret = BOGUS;

	switch (expr->op) {
	case '&':
		ret = handle_ampersand(undefined, implied);
		break;
	case '!':
		ret = !_get_value(expr->unop, undefined, implied);
		break;
	case '~':
		ret = ~_get_value(expr->unop, undefined, implied);
		ret = cast_to_type(expr->unop, ret);
		break;
	case '-':
		ret = -_get_value(expr->unop, undefined, implied);
		break;
	case '*':
		ret = _get_implied_value(expr, undefined, implied);
		break;
	case '(':
		ret = handle_expression_statement(expr, undefined, implied);
		break;
	default:
		*undefined = 1;
	}
	return ret;
}

static long long handle_divide(struct expression *expr, int *undefined, int implied)
{
	long long left;
	long long right;
	long long ret = BOGUS;

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, opposite_implied(implied));

	if (right == 0)
		*undefined = 1;
	else
		ret = left / right;

	return ret;
}

static long long handle_subtract(struct expression *expr, int *undefined, int implied)
{
	long long left;
	long long right;

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, opposite_implied(implied));

	return left - right;
}

static long long handle_binop(struct expression *expr, int *undefined, int implied)
{
	long long left;
	long long right;
	long long ret = BOGUS;

	if (expr->type != EXPR_BINOP) {
		*undefined = 1;
		return ret;
	}

	left = _get_value(expr->left, undefined, implied);
	right = _get_value(expr->right, undefined, implied);

	switch (expr->op) {
	case '*':
		ret =  left * right;
		break;
	case '/':
		ret = handle_divide(expr, undefined, implied);
		break;
	case '+':
		ret = left + right;
		break;
	case '-':
		ret = handle_subtract(expr, undefined, implied);
		break;
	case '%':
		if (right == 0)
			*undefined = 1;
		else
			ret = left % right;
		break;
	case '|':
		ret = left | right;
		break;
	case '&':
		ret = left & right;
		break;
	case SPECIAL_RIGHTSHIFT:
		ret = left >> right;
		break;
	case SPECIAL_LEFTSHIFT:
		ret = left << right;
		break;
	case '^':
		ret = left ^ right;
		break;
	default:
		*undefined = 1;
	}
	return ret;
}

static int do_comparison(struct expression *expr)
{
	struct range_list *left_ranges = NULL;
	struct range_list *right_ranges = NULL;
	int poss_true, poss_false;

	get_implied_range_list(expr->left, &left_ranges);
	get_implied_range_list(expr->right, &right_ranges);

	poss_true = possibly_true_range_lists(left_ranges, expr->op, right_ranges);
	poss_false = possibly_false_range_lists(left_ranges, expr->op, right_ranges);

	free_range_list(&left_ranges);
	free_range_list(&right_ranges);

	if (!poss_true && !poss_false)
		return 0;
	if (poss_true && !poss_false)
		return 1;
	if (!poss_true && poss_false)
		return 2;
	return 3;
}

static long long handle_comparison(struct expression *expr, int *undefined, int implied)
{
	int res;

	/* TODO: we should be able to handle this...  */
	if (implied == NOTIMPLIED) {
		*undefined = 1;
		return BOGUS;
	}

	res = do_comparison(expr);
	if (res == 1)
		return 1;
	if (res == 2)
		return 0;

	if (implied == IMPLIED_MIN || implied == FUZZYMIN)
		return 0;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX)
		return 1;

	*undefined = 1;
	return BOGUS;
}

static long long handle_logical(struct expression *expr, int *undefined, int implied)
{
	long long left_val, right_val;

	/* TODO: we should be able to handle this...  */
	if (implied == NOTIMPLIED) {
		*undefined = 1;
		return BOGUS;
	}

	if (get_implied_value(expr->left, &left_val) &&
			get_implied_value(expr->right, &right_val)) {
		switch (expr->op) {
		case SPECIAL_LOGICAL_OR:
			return left_val || right_val;
		case SPECIAL_LOGICAL_AND:
			return left_val && right_val;
		default:
			*undefined = 1;
			return BOGUS;
		}
	}

	if (implied == IMPLIED_MIN || implied == FUZZYMIN)
		return 0;
	if (implied == IMPLIED_MAX || implied == FUZZYMAX)
		return 1;

	*undefined = 1;
	return BOGUS;
}

static int get_implied_value_helper(struct expression *expr, long long *val, int what)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	if (get_value(expr, val))
		return 1;

	name = get_variable_from_expr(expr, &sym);
	if (!name)
		return 0;
	state = get_state(SMATCH_EXTRA, name, sym);
	free_string(name);
	if (!state || !state->data)
		return 0;
	if (what == IMPLIED)
		return estate_get_single_value(state, val);
	if (what == IMPLIED_MAX) {
		*val = estate_max(state);
		if (*val == whole_range.max) /* this means just guessing */
			return 0;
		return 1;
	}
	*val = estate_min(state);
	if (*val == whole_range.min)
		return 0;
	return 1;
}

static int get_fuzzy_max_helper(struct expression *expr, long long *max)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	if (get_implied_max(expr, max))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	*max = whole_range.min;
	FOR_EACH_PTR(sm->possible, tmp) {
		long long new_min;

		new_min = estate_min(tmp->state);
		if (new_min > *max)
			*max = new_min;
	} END_FOR_EACH_PTR(tmp);

	if (*max > whole_range.min)
		return 1;
	return 0;
}

static int get_fuzzy_min_helper(struct expression *expr, long long *min)
{
	struct sm_state *sm;
	struct sm_state *tmp;

	if (get_implied_min(expr, min))
		return 1;

	sm = get_sm_state_expr(SMATCH_EXTRA, expr);
	if (!sm)
		return 0;

	*min = whole_range.max;
	FOR_EACH_PTR(sm->possible, tmp) {
		long long new_max;

		new_max = estate_max(tmp->state);
		if (new_max < *min)
			*min = new_max;
	} END_FOR_EACH_PTR(tmp);

	if (*min < whole_range.max)
		return 1;
	return 0;
}

static long long _get_implied_value(struct expression *expr, int *undefined, int implied)
{
	long long ret = BOGUS;

	switch (implied) {
	case IMPLIED:
	case IMPLIED_MAX:
	case IMPLIED_MIN:
		if (!get_implied_value_helper(expr, &ret, implied))
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

static int get_const_value(struct expression *expr, long long *val)
{
	struct symbol *sym;

	sym = expr->symbol;
	if (!sym)
		return 0;
	if (!(sym->ctype.modifiers & MOD_CONST))
		return 0;
	if (get_value(sym->initializer, val))
		return 1;
	return 0;
}

static long long _get_value(struct expression *expr, int *undefined, int implied)
{
	long long ret = BOGUS;

	if (!expr) {
		*undefined = 1;
		return BOGUS;
	}
	if (*undefined)
		return BOGUS;

	expr = strip_parens(expr);

	switch (expr->type) {
	case EXPR_VALUE:
		ret = expr->value;
		ret = cast_to_type(expr, ret);
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
		return cast_to_type(expr, ret);
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
		ret = get_expression_value_nomod(expr);
		break;
	case EXPR_SYMBOL:
		if (get_const_value(expr, &ret))
			break;
	default:
		ret = _get_implied_value(expr, undefined, implied);
	}
	if (*undefined)
		return BOGUS;
	return ret;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val = _get_value(expr, &undefined, NOTIMPLIED);
	if (undefined)
		return 0;
	return 1;
}

int get_implied_value(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, &undefined, IMPLIED);
	return !undefined;
}

int get_implied_min(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, &undefined, IMPLIED_MIN);
	return !undefined;
}

int get_implied_max(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, &undefined, IMPLIED_MAX);
	return !undefined;
}

int get_fuzzy_min(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, &undefined, FUZZYMIN);
	return !undefined;
}

int get_fuzzy_max(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, &undefined, FUZZYMAX);
	return !undefined;
}

int get_absolute_min(struct expression *expr, long long *val)
{
	struct symbol *type;
	long long min;

	type = get_type(expr);
	if (!type) {
		if (get_value(expr, val))
			return 1;
		return 0;
	}
	min = type_min(type);
	if (!get_implied_min(expr, val) || *val < min)
		*val = min;
	return 1;
}

int get_absolute_max(struct expression *expr, long long *val)
{
	struct symbol *type;
	long long max;

	type = get_type(expr);
	if (!type) {
		if (get_value(expr, val))
			return 1;
		return 0;
	}
	max = type_max(type);
	if (!get_implied_max(expr, val) || *val > max)
		*val = max;
	if (*val < type_min(type))
		*val = max;
	return 1;
}

int known_condition_true(struct expression *expr)
{
	long long tmp;

	if (!expr)
		return 0;

	if (get_value(expr, &tmp) && tmp)
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
	long long tmp;

	if (!expr)
		return 0;

	if (known_condition_true(expr))
		return 1;
	if (get_implied_value(expr, &tmp) && tmp)
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
	long long val;

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
		if (get_implied_value(expr, &val) && val == 0)
			return 1;
		break;
	}
	return 0;
}
