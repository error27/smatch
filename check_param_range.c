/*
 * sparse/check_param_range.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static int have_returned_zero;
static struct range_list *param_constraints[16];


static struct statement *prev_statement(void)
{
	struct statement *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR_REVERSE(big_statement_stack, tmp) {
		if (i++ == 1)
			return tmp;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return NULL;
}

/*
 * on_main_path() is supposed to check for nesting.  As a hack it just counts
 * the current indent level.
 */
static int on_main_path(struct expression *expr)
{
	if (expr->pos.pos == 24)
		return 1;
	return 0;
}

static int is_error_value(struct expression *ret_value)
{
	sval_t sval;
	char *name;

	if (!ret_value)
		return 0;

	if (ret_value->type != EXPR_PREOP || ret_value->op != '-')
		return 0;

	if (!get_value(ret_value, &sval))
		return 0;
	if (sval.value < -4095 || sval.value >= 0)
		return 0;

	name = pos_ident(ret_value->unop->pos);
	if (!name)
		return 0;
	if (name[0] == 'E')
		return 1;
	return 0;
}

static struct expression *get_param(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *tmp;

	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_SYMBOL)
		return 0;

	sym = expr->symbol;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return expr;
	} END_FOR_EACH_PTR(tmp);

	return NULL;
}

static int get_param_num(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *tmp;
	int i;

	expr = strip_expr(expr);
	if (!expr || expr->type != EXPR_SYMBOL)
		return -1;

	sym = expr->symbol;
	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(tmp);

	return -1;
}

static void add_param_constraint(int idx, struct range_list *rl)
{
	if (!param_constraints[idx]) {
		param_constraints[idx] = rl;
		return;
	}
	param_constraints[idx] = range_list_union(param_constraints[idx], rl);
}

static void handle_condition(struct expression *expr)
{
	struct sm_state *sm;
	struct expression *param;
	struct state_list *slist = NULL;
	char *name;
	struct symbol *sym;

	expr = strip_expr(expr);
	if (!expr)
		return;

	switch (expr->type) {
	case EXPR_LOGICAL:
		if (expr->op == SPECIAL_LOGICAL_OR) {
			handle_condition(expr->left);
			handle_condition(expr->right);
		}
		return;
	case EXPR_COMPARE:
		param = get_param(expr->left);
		if (param)
			break;
		param = get_param(expr->right);
		if (param)
			break;
		return;
	case EXPR_PREOP:
		if (expr->op == '!') {
			param = get_param(expr->unop);
			if (param)
				break;
		}
		return;
	case EXPR_SYMBOL:
		param = get_param(expr);
		if (param)
			break;
		return;
	default:
		return;
	}

	name = get_variable_from_expr(param, &sym);
	if (!name || !sym)
		goto free;

	__push_fake_cur_slist();
	__split_whole_condition(expr);

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (sm) {
		int num;

		num = get_param_num(param);
		add_param_constraint(num, estate_ranges(sm->state));
	}

	__push_true_states();
	__use_false_states();
	__merge_true_states();
	slist = __pop_fake_cur_slist();

free:
	free_string(name);
	free_slist(&slist);
}

static void match_return(struct expression *ret_value)
{
	struct statement *stmt;

	if (have_returned_zero)
		return;
	if (possibly_true(ret_value, SPECIAL_EQUAL, zero_expr())) {
		have_returned_zero = 1;
		return;
	}

	if (!on_main_path(ret_value))  /* should we just set have_returned_zero here? */
		return;

	if (!is_error_value(ret_value))
		return;
	stmt = prev_statement();
	if (!stmt || stmt->type != STMT_IF)
		return;
	handle_condition(stmt->if_conditional);
}

static void match_end_func(struct symbol *sym)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(param_constraints); i++) {
		if (!param_constraints[i])
			continue;
		if (is_whole_range_rl(param_constraints[i]))
			continue;
		sm_msg("info: %s param %d range '%s' implies error return %s",
		       global_static(), i, show_ranges(param_constraints[i]),
		       global_static());
	}

	have_returned_zero = 0;
	for (i = 0; i < ARRAY_SIZE(param_constraints); i++)
		param_constraints[i] = NULL;
}

void check_param_range(int id)
{
	if (!option_info || option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
