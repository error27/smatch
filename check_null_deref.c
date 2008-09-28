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

STATE(ignore);
STATE(isnull);
STATE(nonnull);

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	if (s1 == &ignore || s2 == &ignore)
		return &ignore;
	if (s1 == NULL && s2 == &nonnull)
		return &nonnull;
	return &undefined;
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
		tmp = strip_expr(tmp);
		if (tmp->op == '&') {
			name = get_variable_from_expr_simple(tmp->unop, &sym);
			if (name) {
				name = alloc_string(name);
				set_state(name, my_id, sym, &nonnull);
			}
		} else {
			name = get_variable_from_expr_simple(tmp, &sym);
			if (name && sym) {
				struct smatch_state *state = get_state(name, my_id, sym);
				if ( state == &isnull)
					smatch_msg("Null param %s %d", 
						   func, i);
				else if (state == &undefined)
					smatch_msg("Undefined param %s %d", 
						   func, i);
			}
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static int assign_seen;
static void match_assign(struct expression *expr)
{
	struct expression *left;
	struct symbol *sym;
	char *name;
	
	if (assign_seen) {
		assign_seen--;
		return;
	}
	left = strip_expr(expr->left);
	name = get_variable_from_expr_simple(left, &sym);
	if (!name)
		return;
	name = alloc_string(name);
	if (is_zero(expr->right))
		set_state(name, my_id, sym, &isnull);
	else
		set_state(name, my_id, sym, &nonnull);
}

/*
 * set_new_true_false_states is used in the following conditions
 * if (a) { ... if (a) { ... } } 
 * The problem is that after the second blog a is set to undefined
 * even though the second condition is meaning less.  (The second test
 * could be a macro for example).
 */

static void set_new_true_false_states(const char *name, int my_id, 
				      struct symbol *sym, struct smatch_state *true_state,
				      struct smatch_state *false_state)
{
	struct smatch_state *tmp;

	tmp = get_state(name, my_id, sym);
	
	SM_DEBUG("set_new_stuff called at %d value='%s'\n", get_lineno(), show_state(tmp));

	if (!tmp || tmp == &undefined || tmp == &isnull)
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
		set_new_true_false_states(name, my_id, sym, &nonnull, &isnull);
		return;
	case EXPR_ASSIGNMENT:
		assign_seen++;
                /*
		 * There is a kernel macro that does
		 *  for ( ... ; ... || x = NULL ; ) ...
		 */
		if (is_zero(expr->right)) {
			name = get_variable_from_expr_simple(expr->left, &sym);
			if (!name)
				return;
			name = alloc_string(name);
			set_new_true_false_states(name, my_id, sym, NULL, &isnull);
			return;
		}
		 /* You have to deal with stuff like if (a = b = c) */
		match_condition(expr->right);
		match_condition(expr->left);
		return;
	default:
		return;
	}
}

static void match_declarations(struct symbol *sym)
{
	const char * name;

	if ((get_base_type(sym))->type == SYM_ARRAY) {
		return;
	}

	name = sym->ident->name;

	if (sym->initializer) {
		if (is_zero(sym->initializer))
			set_state(name, my_id, sym, &isnull);
		else
			set_state(name, my_id, sym, &nonnull);
	} else {
		set_state(name, my_id, sym, &undefined);
	}
}

static void match_dereferences(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;
	struct smatch_state *state;

	if (strcmp(show_special(expr->deref->op), "*"))
		return;

	deref = get_variable_from_expr_simple(expr->deref->unop, &sym);
	if (!deref)
		return;
	deref = alloc_string(deref);

	state = get_state(deref, my_id, sym);
	if (state == &undefined) {
		smatch_msg("Dereferencing Undefined:  '%s'", deref);
		set_state(deref, my_id, sym, &ignore);
	} else if (state == &isnull) {
		/* It turns out that you only get false positives from 
		   this.  Mostly foo = NULL; sizeof(*foo); */
		/* smatch_msg("Error dereferencing NULL:  %s", deref); */
		set_state(deref, my_id, sym, &ignore);
	} else {
		free_string(deref);
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
