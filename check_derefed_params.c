/*
 * sparse/check_deference.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include "token.h"
#include "smatch.h"

static int my_id;

enum states {
	ARGUMENT,
	ISNULL,
	NONNULL,
	IGNORE,
};

struct param {
	struct symbol *sym;
	int used;
};
ALLOCATOR(param, "parameters");
#define MAX_PARAMS 16
struct param *params[MAX_PARAMS];

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;
	int i = 0;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		set_state("", my_id, arg, ARGUMENT);
		params[i] = __alloc_param(0);
		params[i]->sym = arg;
		params[i]->used = 0;
		if (i > MAX_PARAMS - 2) {
			printf("Error function has too many params.\n");
			i++;
			break;
		}
		i++;
	} END_FOR_EACH_PTR(arg);
	params[i] = NULL;
}

static void print_unchecked_param(struct symbol *sym) {
	int i;
	for (i = 0; i < (sizeof(*params)/sizeof(params[0])) ; i++) {
		if (params[i]->sym == sym && !params[i]->used) {
			smatch_msg("unchecked param:  %s %d", get_function(),
				   i);
		}
	}
}

static void match_deref(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;

	deref = get_variable_from_expr(expr->deref, &sym);
	switch(get_state("", my_id, sym)) {
	case ARGUMENT:
		print_unchecked_param(sym);
		/* this doesn't actually work because we'll still see
		   the same variable get derefed on other paths */
		set_state("", my_id, sym, IGNORE);
		break;
	case ISNULL:
		smatch_msg("Error dereferencing NULL:  %s", deref);
		break;
	default:
		break;
	}
}

static void match_function_call_after(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	int state;

	FOR_EACH_PTR(expr->args, tmp) {
		if (tmp->op == '&') {
			name = get_variable_from_expr(tmp, &sym);
			state = get_state("", my_id, sym);
			if (state != NOTFOUND) {
				set_state("", my_id, sym, NONNULL);
			}
		}
	} END_FOR_EACH_PTR(tmp);
}

static void match_assign(struct expression *expr)
{
	int state;

	/* Since we're only tracking arguments, we only want 
	   EXPR_SYMBOLs.  */
	if (expr->left->type != EXPR_SYMBOL)
		return;
	state = get_state("", my_id, expr->left->symbol);
	if (state == NOTFOUND)
		return;
	/* probably it's non null */
	set_state("", my_id, expr->left->symbol, NONNULL);
}

static void match_condition(struct expression *expr)
{
	switch(expr->type) {
	case EXPR_COMPARE: {
		struct symbol *sym = NULL;

		if (expr->left->type == EXPR_SYMBOL) {
			sym = expr->left->symbol;
			if (expr->right->type != EXPR_VALUE 
			    || expr->right->value != 0)
				return;
		}
		if (expr->right->type == EXPR_SYMBOL) {
			sym = expr->right->symbol;
			if (expr->left->type != EXPR_VALUE 
			    || expr->left->value != 0)
				return;
		}
		if (!sym)
			return;

		if (expr->op == SPECIAL_EQUAL)
			set_true_false_states("", my_id, sym, ISNULL,
					      NONNULL);
		else if (expr->op == SPECIAL_NOTEQUAL)
			set_true_false_states("", my_id, sym, NONNULL,
					      ISNULL);
		return;
	}
	case EXPR_SYMBOL:
		if (get_state("", my_id, expr->symbol) == ARGUMENT) {
			set_true_false_states("", my_id, expr->symbol, 
		  			      NONNULL, ISNULL);
		}
		return;
	default:
		return;
	}
}

static void end_of_func_cleanup(struct symbol *sym)
{
	int i = 0;
	while (params[i]) {
		__free_param(params[i]);
		i++;
	}
}

void register_derefed_params(int id)
{
	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call_after, FUNCTION_CALL_AFTER_HOOK);
	add_hook(&match_deref, DEREF_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&end_of_func_cleanup, END_FUNC_HOOK);
}
