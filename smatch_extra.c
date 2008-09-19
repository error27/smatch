/*
 * sparse/smatch_extra.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"

static int my_id;

static struct smatch_state *alloc_state(int val)
{
	struct smatch_state *state;

	state = malloc(sizeof(*state));
	state->name = "value";
	state->data = malloc(sizeof(int));
	*(int *)state->data = val;
	return state;
}

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	return &undefined;
}

static void match_function_call_after(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	int i = 0;

	FOR_EACH_PTR(expr->args, tmp) {
		if (tmp->op == '&') {
			name = get_variable_from_expr_simple(tmp->unop, &sym);
			if (name) {
				name = alloc_string(name);
				set_state(name, my_id, sym, &undefined);
			}
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void match_assign(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	
	name = get_variable_from_expr_simple(expr->left, &sym);
	if (!name)
		return;
	name = alloc_string(name);
	set_state(name, my_id, sym, alloc_state(get_value(expr->right, NULL)));
}

static void undef_expr(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	
	name = get_variable_from_expr_simple(expr->unop, &sym);
	if (!name)
		return;
	if (!get_state(name, my_id, sym))
		return;
	name = alloc_string(name);
	set_state(name, my_id, sym, &undefined);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if (sym->ident) {
		name = sym->ident->name;
		if (sym->initializer) {
			set_state(name, my_id, sym, alloc_state(get_value(sym->initializer, NULL)));
		}
	}
}

void register_smatch_extra(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&undef_expr, OP_HOOK);
	add_hook(&match_function_call_after, FUNCTION_CALL_AFTER_HOOK);
	add_hook(&match_assign, ASSIGNMENT_AFTER_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
}

static int expr_to_val(struct expression *expr)
{
	struct smatch_state *state;
	int val;
	struct symbol *sym;
	char *name;
	
	val = get_value(expr, NULL);
	if (val != UNDEFINED)
		return val;

	name = get_variable_from_expr_simple(expr, &sym);
	if (!name)
		return UNDEFINED;
	state = get_state(name, my_id, sym);
	if (!state || !state->data)
		return UNDEFINED;
	return *(int *)state->data;
}

static int true_comparison(int left, int comparison, int right)
{
	switch(comparison){
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left < right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left < right)
			return 1;
	case SPECIAL_EQUAL:
		if (left == right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left == right)
			return 1;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left > right)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left != right)
			return 1;
		return 0;
	default:
		smatch_msg("unhandled comparison %d\n", comparison);
	}
	return 0;
}

int known_condition_true(struct expression *expr)
{
	int left, right, ret;

	if (!expr || expr->type != EXPR_COMPARE)
		return 0;

	if ((left = expr_to_val(expr->left)) == UNDEFINED)
		return 0;

	if ((right = expr_to_val(expr->right)) == UNDEFINED)
		return 0;
	
	ret = true_comparison(left, expr->op, right);
	smatch_msg("known condition: %d & %d => %s", left, right, (ret?"true":"false"));

	return ret;
}
