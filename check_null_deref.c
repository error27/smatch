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
#include "smatch_slist.h"

/* 
 * TODO:  The return_null list of functions should be determined automatically
 */
static const char *return_null[] = {
	"kmalloc",
};

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
STATE(arg_nonnull);
STATE(arg_null);
STATE(assumed_nonnull);
STATE(ignore);
STATE(isnull);
STATE(nonnull);

static struct symbol *func_sym;

/*
 * merge_func() is to handle assumed_nonnull.
 *
 * Assumed_nonnull is a funny state.  It's almost the same as 
 * no state.  Maybe it would be better to delete it completely...
 */
static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	if (s1 == &ignore || s2 == &ignore)
		return &ignore;
	return &merged;

}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (sm->state == &assumed_nonnull)
		return &assumed_nonnull;
	return &undefined;
}

static int is_maybe_null_no_arg(const char *name, struct symbol *sym)
{
	struct state_list *slist;
	struct sm_state *tmp;
	int ret = 0;

	slist = get_possible_states(my_id, name, sym);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &ignore)
			return 0;
		if (tmp->state == &isnull) {
			sm_debug("is_maybe_null_no_arg() says %s &isnull\n", name);
			ret = 1;
		} 
		if (tmp->state == &undefined) {
			sm_debug("is_maybe_null_no_arg() says %s &undefined\n", name);
			ret = 1;
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}


static int is_maybe_null(const char *name, struct symbol *sym)
{
	struct state_list *slist;
	struct sm_state *tmp;
	int ret = 0;

	slist = get_possible_states(my_id, name, sym);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &ignore)
			return 0;
		if (tmp->state == &isnull) {
			sm_debug("is_maybe_null() says %s &isnull\n", name);
			ret = 1;
		} 
		if (tmp->state == &undefined) {
			sm_debug("is_maybe_null() says %s &undefined\n", name);
			ret = 1;
		}
		if (tmp->state == &arg_null) {
			sm_debug("is_maybe_null() says %s &arg_null\n", name);
			ret = 1;
		}
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

static int is_maybe_null_arg(char *name, struct symbol *sym)
{
	struct state_list *slist;
	struct sm_state *tmp;
	int maybe_null = 0;

	slist = get_possible_states(my_id, name, sym);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state != &argument && tmp->state != &arg_null && 
		    tmp->state != &arg_nonnull && tmp->state !=  &merged &&
		    tmp->state != &assumed_nonnull)
			return 0;
		if (tmp->state == &argument || tmp->state == &arg_null)
			maybe_null = 1;
	} END_FOR_EACH_PTR(tmp);

	/* We really only care about arguments if they are null */
	return maybe_null;
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
		set_state(my_id, arg->ident->name, arg, &argument);
	} END_FOR_EACH_PTR(arg);
}

static void match_function_call_after(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	struct symbol *func = NULL;
	int i;

	if (expr->fn->type == EXPR_SYMBOL)
		func = expr->fn->symbol;

	i = 0;
	FOR_EACH_PTR(expr->args, tmp) {
		tmp = strip_expr(tmp);
		if (tmp->type == EXPR_PREOP && tmp->op == '&') {
			set_state_expr(my_id, tmp->unop, &assumed_nonnull);
		} else if (func) {
			name = get_variable_from_expr(tmp, &sym);
			if (final_pass && name && is_maybe_null_no_arg(name, sym))
				add_param(&calls, func, i, get_lineno());
			free_string(name);
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void match_assign_returns_null(const char *fn, struct expression *expr,
				      void *unused)
{
	set_state_expr(my_id, expr->left, &undefined);
}

static void match_assign(struct expression *expr)
{
	if (is_zero(expr->right)) {
		set_state_expr(my_id, expr->left, &isnull);
		return;
	}
	/* hack alert.  we set the state to here but it might get
	   set later to &undefined in match_function_assign().
	   By default we assume it's assigned something nonnull here. */
	set_state_expr(my_id, expr->left, &assumed_nonnull);
}

/*
 * set_new_true_false_paths() is used in the following conditions
 * if (a) { ... if (a) { ... } } 
 * The problem is that after the second blog a is set to undefined
 * even though the second condition is meaning less.  (The second test
 * could be a macro for example).
 *
 * Also, stuff passed to the condition hook is processed behind the scenes
 * a bit.  Instead of a condition like (foo == 0) which is true when foo is
 * null, we get just (foo) and the true and false states are adjusted later.
 * Basically the important thing to remember, is that for us, true is always
 * non null and false is always null.
 */
static void set_new_true_false_paths(struct expression *expr, int recently_assigned)
{
	struct smatch_state *tmp;
	char *name;
	struct symbol *sym;


	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;

	tmp = get_state_expr(my_id, expr);
	sm_debug("set_new_stuff called at for %s on line %d value='%s'\n", 
		name, get_lineno(), show_state(tmp));
       
	if (tmp == &argument) {
		set_true_false_states_expr(my_id, expr, &arg_nonnull, &arg_null);
		goto free;
	}

	if (tmp == &ignore) {
		set_true_false_states_expr(my_id, expr, &ignore, &ignore);
		goto free;
	}

	if (!tmp || recently_assigned || is_maybe_null(name, sym)) {
		set_true_false_states_expr(my_id, expr, &nonnull, &isnull);
		goto free;
	}
free:
	free_string(name);

}

static void match_condition(struct expression *expr)
{
	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_PREOP:
	case EXPR_SYMBOL:
	case EXPR_DEREF:
		set_new_true_false_paths(expr, 0);
		return;
	case EXPR_ASSIGNMENT:
                /*
		 * There is a kernel macro that does
		 *  for ( ... ; ... || x = NULL ; ) ...
		 */
		if (is_zero(expr->right)) {
			set_true_false_states_expr(my_id, expr->left, NULL, &isnull);
			return;
		}
		set_new_true_false_paths(expr->left, 1);
		return;
	}
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if ((get_base_type(sym))->type == SYM_ARRAY) {
		return;
	}

	name = sym->ident->name;

	if (!sym->initializer) {
		set_state(my_id, name, sym, &undefined);
		scoped_state(name, my_id, sym);
	}
}

static void match_dereferences(struct expression *expr)
{
	char *deref = NULL;
	struct symbol *sym = NULL;

	if (strcmp(show_special(expr->deref->op), "*"))
		return;

	deref = get_variable_from_expr(expr->deref->unop, &sym);
	if (!deref)
		return;

	if (is_maybe_null_arg(deref, sym)) {
		add_do_not_call(sym, get_lineno());
		set_state(my_id, deref, sym, &assumed_nonnull);
	} else if (is_maybe_null(deref, sym)) {
		sm_msg("error: dereferencing undefined:  '%s'", deref);
		set_state(my_id, deref, sym, &ignore);
	}
	free_string(deref);
}

static void end_file_processing(void)
{
	struct func_n_param *param1, *param2;

	// if there is an error print it out...
	FOR_EACH_PTR(calls, param1) {
		FOR_EACH_PTR(do_not_call, param2) {
			if (param1->func == param2->func && 
			    param1->param == param2->param)
				sm_printf("%s +%d error: cross_func deref %s %d\n", 
				       get_filename(), param1->line, 
				       param1->func->ident->name,
				       param1->param);
		} END_FOR_EACH_PTR(param2);
	} END_FOR_EACH_PTR(param1);

	if (!option_spammy)
		return;

	// if a function is not static print it out...
	FOR_EACH_PTR(do_not_call, param1) {
		if (!(param1->func->ctype.modifiers & MOD_STATIC))
			sm_printf("%s +%d info: unchecked param %s %d\n", 
			       get_filename(), param1->line,
			       param1->func->ident->name, param1->param);
	} END_FOR_EACH_PTR(param1);
	
	// if someone calls a non-static function print that..
	FOR_EACH_PTR(calls, param1) {
		int defined = 0;
		
		FOR_EACH_PTR(funcs, param2) {
			if (param1->func == param2->func)
				defined = 1;
		} END_FOR_EACH_PTR(param2);
		if (!defined)
			sm_printf("%s +%d info: undefined param %s %d\n",
			       get_filename(), param1->line,
			       param1->func->ident->name, param1->param);
	} END_FOR_EACH_PTR(param1);
}

static void register_allocation_funcs(void)
{
	struct token *token;
	const char *func;

	token = get_tokens_file("kernel.allocation_funcs");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		add_function_assign_hook(func, &match_assign_returns_null,
					 NULL);
		token = token->next;
	}
	clear_token_alloc();
}

void check_null_deref(int id)
{
	int i;

	my_id = id;
	set_default_state(my_id, &assumed_nonnull);
	add_merge_hook(my_id, &merge_func);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call_after, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&end_file_processing, END_FILE_HOOK);

	for(i = 0; i < sizeof(*return_null)/sizeof(return_null[0]); i++) {
		add_function_assign_hook(return_null[i],
					 &match_assign_returns_null, NULL);
	}
	register_allocation_funcs();
}
