/*
 * smatch/check_uncapped_alloc.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(capped);
STATE(untrusted);

static struct symbol_list *untrusted_syms;

static int db_callback(void *unused, int argc, char **argv, char **azColName)
{
	struct symbol *arg;
	unsigned int param;
	int i;
	int dummy = 0;

	errno = 0;
	param = strtoul(argv[0], NULL, 10);
	if (errno)
		return dummy;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		if (i == param)
			add_ptr_list(&untrusted_syms, arg);
		i++;
	} END_FOR_EACH_PTR(arg);

	return dummy;
}

static void match_function_def(struct symbol *sym)
{
	if (!sym || !sym->ident || !sym->ident->name)
		return;

	run_sql(db_callback,
		"select distinct parameter from untrusted where function = '%s';",
		sym->ident->name);
}

static int is_untrusted_sym(struct symbol *sym)
{
	struct symbol *tmp;

	FOR_EACH_PTR(untrusted_syms, tmp) {
		if (tmp == sym)
			return 1;
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

static int is_untrusted_data(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	int is_untrusted = 0;
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &untrusted))
		return 1;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;
	is_untrusted = is_untrusted_sym(sym);
free:
	free_string(name);
	return is_untrusted;
}

static void match_condition(struct expression *expr)
{
	struct smatch_state *left_true = NULL;
	struct smatch_state *left_false = NULL;
	struct smatch_state *right_true = NULL;
	struct smatch_state *right_false = NULL;


	switch (expr->op) {
	case '<':
	case SPECIAL_LTE:
	case SPECIAL_UNSIGNED_LT:
	case SPECIAL_UNSIGNED_LTE:
		left_true = &capped;
		right_false = &capped;
		break;
	case '>':
	case SPECIAL_GTE:
	case SPECIAL_UNSIGNED_GT:
	case SPECIAL_UNSIGNED_GTE:
		left_false = &capped;
		right_true = &capped;
		break;
	default:
		return;
	}
	if (is_untrusted_data(expr->left))
		set_true_false_states_expr(my_id, expr->left, left_true, left_false);
	if (is_untrusted_data(expr->right))
		set_true_false_states_expr(my_id, expr->right, right_true, right_false);
}

static int is_uncapped(struct expression *expr)
{
	long long max;

	if (expr->type == EXPR_BINOP) {
		if (is_uncapped(expr->left))
			return 1;
		if (is_uncapped(expr->right))
			return 1;
		return 0;
	}
	if (!is_untrusted_data(expr))
		return 0;
	if (get_implied_max(expr, &max))
		return 0;
	if (get_absolute_max(expr, &max) && max <= 4096)
		return 0;
	if (get_state_expr(my_id, expr) == &capped)
		return 0;
	return 1;
}

static void match_assign(struct expression *expr)
{
	struct expression *left;

	left = strip_expr(expr->left);
	if (is_untrusted_data(left))
		set_state_expr(my_id, left, &capped);
	if (is_uncapped(expr->right))
		set_state_expr(my_id, left, &untrusted);
}

static void match_uncapped_max(const char *fn, struct expression *expr, void *_arg_nr)
{
	int arg_nr = PTR_INT(_arg_nr);
	struct expression *arg;
	char *name;

	arg = get_argument_from_call_expr(expr->args, arg_nr);
	if (!is_uncapped(arg))
		return;
	name = get_variable_from_expr_complex(arg, NULL);
	sm_msg("warn: untrusted data is not capped '%s'", name);
	free_string(name);
}

static void match_func_end(struct symbol *sym)
{
	__free_ptr_list((struct ptr_list **)&untrusted_syms);
}

void check_uncapped_alloc(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	if (option_no_db)
		return;
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_function_hook("kmalloc", &match_uncapped_max, INT_PTR(0));
	add_function_hook("kzalloc", &match_uncapped_max, INT_PTR(0));

	add_function_hook("copy_to_user", &match_uncapped_max, INT_PTR(2));
	add_function_hook("_copy_to_user", &match_uncapped_max, INT_PTR(2));
	add_function_hook("__copy_to_user", &match_uncapped_max, INT_PTR(2));
	add_function_hook("copy_from_user", &match_uncapped_max, INT_PTR(2));
	add_function_hook("_copy_from_user", &match_uncapped_max, INT_PTR(2));
	add_function_hook("__copy_from_user", &match_uncapped_max, INT_PTR(2));

	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_hook(&match_func_end, END_FUNC_HOOK);
}
