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

struct func_n_param {
	struct symbol *func;
	int param;
	int line;
};
ALLOCATOR(func_n_param, "func parameters");
DECLARE_PTR_LIST(param_list, struct func_n_param);

static struct param_list *funcs;
static struct param_list *do_not_call;
static struct param_list *calls;

static int my_id;

STATE(argument);
STATE(assumed_nonnull);
STATE(ignore);
STATE(isnull);
STATE(nonnull);

static struct symbol *func_sym;

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	/* 
	 * conditions are a special case.  In the cond_(true|false)_stack
	 * we expect to be merging null with a new specified state all the
	 * time.  Outside of a condition we have things where the code
	 * assumes a global variable is non null.  That gets merged with
	 * other code and it becomes undefined.  But really it should be 
	 * non-null.
	 * In theory we could test for that latter case by printing a message
	 * when someone checks a variable we already had marked as non-null.
	 * In practise that didn't work really well because a lot of macros
	 * have "unneeded" checks for null.
	 */
	if (!in_condition() && s1 == NULL)
		return s2;
	if (s1 == &ignore || s2 == &ignore)
		return &ignore;
	if (s1 == NULL && s2 == &assumed_nonnull)
		return &assumed_nonnull;
	if (s1 == &assumed_nonnull && s2 == &nonnull)
		return &assumed_nonnull;
	if (s1 == &argument && s2 == &assumed_nonnull)
		return &assumed_nonnull;
	if (s1 == &argument && s2 == &nonnull)
		return &nonnull;
	return &undefined;
}

static struct func_n_param *alloc_func_n_param(struct symbol *func, int param,
					       int line)
{
	struct func_n_param *tmp = __alloc_func_n_param(0);

	tmp->func = func;
	tmp->param = param;
	tmp->line = line;
	return tmp;
}

static int get_arg_num(struct symbol *sym)
{
	struct symbol *arg;
	int i = 0;

	FOR_EACH_PTR(func_sym->ctype.base_type->arguments, arg) {
		if (arg == sym) {
			return i;
		}
		i++;
	} END_FOR_EACH_PTR(arg);
	return -1;
}

static void add_do_not_call(struct symbol *sym, int line)
{
	struct func_n_param *tmp;
	int num = get_arg_num(sym);

	FOR_EACH_PTR(do_not_call, tmp) {
		if (tmp->func == func_sym && tmp->param == num)
			return;
	} END_FOR_EACH_PTR(tmp);
	tmp = alloc_func_n_param(func_sym, num, line);
	add_ptr_list(&do_not_call, tmp);
}

static void add_param(struct param_list **list, struct symbol *func, int param,
		      int line)
{
	struct func_n_param *tmp;

	tmp = alloc_func_n_param(func, param, line);
	add_ptr_list(list, tmp);
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	func_sym = sym;
	add_param(&funcs, sym, 0, 0);
	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		if (!arg->ident) {
			continue;
		}
		set_state(arg->ident->name, my_id, arg, &argument);
	} END_FOR_EACH_PTR(arg);
}

static void match_function_call_after(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	struct symbol *func = NULL;
	int i;

	if (expr->fn->type == EXPR_SYMBOL) {
		func = expr->fn->symbol;
	}

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		if (tmp->op == '&') {
			name = get_variable_from_expr_simple(tmp->unop, &sym);
			if (name) {
				name = alloc_string(name);
				set_state(name, my_id, sym, &assumed_nonnull);
			}
		} else {
			name = get_variable_from_expr_simple(tmp, &sym);
			if (func && name && sym)
				if (get_state(name, my_id, sym) == &undefined)
					add_param(&calls, func, i, get_lineno());
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
		set_state(name, my_id, sym, &assumed_nonnull);
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

	if (!tmp || tmp == &undefined || tmp == &isnull || tmp == &argument)
		set_true_false_states(name, my_id, sym, true_state, false_state);
}

static void match_condition(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
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
			set_state(name, my_id, sym, &assumed_nonnull);
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
		/* 
		 * It turns out that you only get false positives from 
		 * this.  Mostly foo = NULL; sizeof(*foo);
		 * And even if it wasn't always a false positive you'd think
		 * it would get caught in testing if it failed every time.
		 */
		set_state(deref, my_id, sym, &ignore);
	} else if (state == &argument) {
		add_do_not_call(sym, get_lineno());
		set_state(deref, my_id, sym, &assumed_nonnull);
	} else {
		free_string(deref);
	}
}

static void end_file_processing()
{
	struct func_n_param *param1, *param2;

	// if a function is not static print it out...
	FOR_EACH_PTR(do_not_call, param1) {
		if (!(param1->func->ctype.modifiers & MOD_STATIC))
			printf("%s +%d unchecked param %s %d\n", 
			       get_filename(), param1->line,
			       param1->func->ident->name, param1->param);
	} END_FOR_EACH_PTR(param1);
	
	// if there is an error print it out...
	FOR_EACH_PTR(calls, param1) {
		FOR_EACH_PTR(do_not_call, param2) {
			if (param1->func == param2->func && 
			    param1->param == param2->param)
				printf("%s +%d cross_func deref %s %d\n", 
				       get_filename(), param1->line, 
				       param1->func->ident->name,
				       param1->param);
		} END_FOR_EACH_PTR(param2);
	} END_FOR_EACH_PTR(param1);

	// if someone calls a non-static function print that..
	FOR_EACH_PTR(calls, param1) {
		int defined = 0;
		
		FOR_EACH_PTR(funcs, param2) {
			if (param1->func == param2->func)
				defined = 1;
		} END_FOR_EACH_PTR(param2);
		if (!defined)
			printf("%s +%d undefined param %s %d\n", get_filename(),
			       param1->line, param1->func->ident->name,
			       param1->param);
	} END_FOR_EACH_PTR(param1);
}

void register_null_deref(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call_after, FUNCTION_CALL_AFTER_HOOK);
	add_hook(&match_assign, ASSIGNMENT_AFTER_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&end_file_processing, END_FILE_HOOK);
}
