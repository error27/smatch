/*
 * sparse/check_deference.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "token.h"
#include "smatch.h"

static int my_id;

enum states {
	ISNULL,
	NONNULL,
	IGNORE,
};

static int merge_func(const char *name, struct symbol *sym, int s1, int s2)
{
	if (s2 == IGNORE)
		return IGNORE;
	return UNDEFINED;
}

static void match_function_call_after(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	char *func = NULL;
	int i = 0;

	if (expr->fn->type == EXPR_SYMBOL) {
		func = expr->fn->symbol_name->name;
	}

	FOR_EACH_PTR(expr->args, tmp) {
		if (tmp->op == '&') {
			name = get_variable_from_expr_simple(tmp->unop, &sym);
			if (name) {
				name = alloc_string(name);
				set_state(name, my_id, sym, NONNULL);
			}
		} else {
			name = get_variable_from_expr_simple(tmp, &sym);
			if (name && sym) {
				int state = get_state(name, my_id, sym);
				if ( state == ISNULL)
					smatch_msg("Null param %s %d", 
						   func, i);
				else if (state == UNDEFINED)
					smatch_msg("Undefined param %s %d", 
						   func, i);
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
	if (is_zero(expr->right))
		set_state(name, my_id, sym, ISNULL);
	else
		set_state(name, my_id, sym, NONNULL);
}

/*
 * set_new_true_false_states is used in the following conditions
 * if (a) { ... if (a) { ... } } 
 * The problem is that after the second blog a is set to undefined
 * even though the second condition is meaning less.  (The second test
 * could be a macro for example).
 */

static void set_new_true_false_states(const char *name, int my_id, 
				      struct symbol *sym, int true_state,
				      int false_state)
{
	int tmp;

	tmp = get_state(name, my_id, sym);

	SM_DEBUG("set_new_stuff called at %d value=%d\n", get_lineno(), tmp);

	if (tmp == NOTFOUND || tmp == UNDEFINED || tmp == ISNULL)
		set_true_false_states(name, my_id, sym, true_state, false_state);
}

static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	switch(expr->type) {
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		name = get_variable_from_expr_simple(expr, &sym);
		if (!name)
			return;
		name = alloc_string(name);
		set_new_true_false_states(name, my_id, sym, NONNULL, ISNULL);
		return;
	case EXPR_ASSIGNMENT:
		match_condition(expr->left);
		return;
	default:
		return;
	}
}

static void match_declarations(struct symbol *sym)
{
	if (sym->ident) {
		const char * name;

		name = sym->ident->name;
		if (sym->initializer) {
			if (is_zero(sym->initializer))
				set_state(name, my_id, sym, ISNULL);
			else
				set_state(name, my_id, sym, NONNULL);
		}
	}
}

static int is_array(struct expression *expr)
{
	struct symbol *tmp = NULL;
	char *name;
	
	name = get_variable_from_expr_simple(expr, NULL);
	if (expr->ctype)
		tmp = get_base_type(expr->ctype);
	if (!tmp || !name)
		return 0;
	if (tmp->type == SYM_PTR)
		tmp = get_base_type(tmp);
	if (!tmp)
		return 0;
	printf("debug: %s %d\n", name, tmp->type);
	if (tmp->ident)
		printf("name %s\n", tmp->ident->name);

	return 0;
	if (expr->type != EXPR_BINOP || expr->op != '+')
		return 0;
	//if (expr->left->ctype && expr->left->ctype.base_type == &ptr_ctype)
	//       return 1;
	return 0;
}

static void match_dereferences(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;

	if (expr->op == '*') {
		expr = expr->unop;
	} else if (expr->type == EXPR_DEREF && expr->deref->op == '*')
		expr = expr->deref->unop;

	/* 
	 * Say you have foo->bar.baz.n 
	 * In that case we really only care about part to the
	 * left of the ->.
	 */
	while (expr->type == EXPR_DEREF && expr->deref->op != '*')
		expr = expr->deref;

	deref = get_variable_from_expr_simple(expr, &sym);
	if (!deref)
		return;
	deref = alloc_string(deref);
	
	switch(get_state(deref, my_id, sym)) {
	case UNDEFINED:
		smatch_msg("Dereferencing Undefined:  %s", deref);
		set_state(deref, my_id, sym, IGNORE);
		break;
	case ISNULL:
		/* It turns out that you only get false positives from 
		   this.  Mostly foo = NULL; sizeof(*foo); */
		/* smatch_msg("Error dereferencing NULL:  %s", deref); */
		set_state(deref, my_id, sym, IGNORE);
		break;
	default:
		free_string(deref);
		break;
	}
}

void register_null_deref(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_function_call_after, FUNCTION_CALL_AFTER_HOOK);
	add_hook(&match_assign, ASSIGNMENT_AFTER_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
}
