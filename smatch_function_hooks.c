#define _GNU_SOURCE
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

ALLOCATOR(fcall_back, "call backs");

static struct hsearch_data func_hash;

static struct state_list *cond_true = NULL;
static struct state_list *cond_false = NULL;
static int in_hook = 0;
#define REGULAR_CALL 0
#define CONDITIONAL_CALL 1
#define ASSIGN_CALL 2

static struct fcall_back *alloc_fcall_back(int type, func_hook *call_back,
					   void *info)
{
	struct fcall_back *cb;

	cb = __alloc_fcall_back(0);
	cb->type = type;
	cb->call_back = call_back;
	cb->info = info;
	return cb;
}

static struct call_back_list *get_call_backs(const char *look_for)
{
	ENTRY e, *ep;

	e.key = (char *)look_for;
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep)
		return NULL;
	return (struct call_back_list *)ep->data;
}

static void add_cb_hook(const char *look_for, struct fcall_back *cb)
{
	ENTRY e, *ep;
	char *old_key = NULL;

	e.key = alloc_string(look_for);
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep) {
		struct call_back_list *list = NULL;
		
		add_ptr_list(&list, cb);
		e.data = list;
	} else {
		old_key = e.key;
		e.key = ep->key;
		add_ptr_list((struct call_back_list **)&ep->data, cb);
		e.data = ep->data;
	}
	if (!hsearch_r(e, ENTER, &ep, &func_hash)) {
		printf("Error hash table too small in smatch_function_hooks.c\n");
		exit(1);
	}
	free_string(old_key);
}

void add_function_hook(const char *look_for, func_hook *call_back, void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(REGULAR_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

void add_conditional_hook(const char *look_for, func_hook *call_back,
			  void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(CONDITIONAL_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

void add_function_assign_hook(const char *look_for, func_hook *call_back,
			      void *info)
{
	struct fcall_back *cb;

	cb = alloc_fcall_back(ASSIGN_CALL, call_back, info);
	add_cb_hook(look_for, cb);
}

static void call_call_backs(struct call_back_list *list, int type,
			    const char *fn, struct expression *expr)
{
	struct fcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->type == type)
			(tmp->call_back)(fn, expr, tmp->info);
	} END_FOR_EACH_PTR(tmp);
}

static void match_function_call(struct expression *expr)
{
	struct call_back_list *call_backs;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;
	call_backs = get_call_backs(expr->fn->symbol->ident->name);
	if (!call_backs)
		return;
	call_call_backs(call_backs, REGULAR_CALL, expr->fn->symbol->ident->name,
			expr);
}

static void assign_condition_funcs(const char *fn, struct expression *expr,
				 struct call_back_list *call_backs)
{
	struct fcall_back *tmp;
	struct sm_state *sm;
	int conditional = 0;
	char *var_name;
	struct symbol *sym;
	struct smatch_state *zero_state, *non_zero_state;

	var_name = get_variable_from_expr(expr->left, &sym);
	if (!var_name || !sym)
		goto free;

	in_hook = 1;
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != CONDITIONAL_CALL)
			continue;

		conditional = 1;
		(tmp->call_back)(fn, expr->right, tmp->info);
	} END_FOR_EACH_PTR(tmp);
	if (conditional) {
		zero_state = alloc_extra_state(0);
		non_zero_state = add_filter(extra_undefined(), 0);
		set_cond_states(var_name, SMATCH_EXTRA, sym, non_zero_state, zero_state);
	}
  	in_hook = 0;

	if (!conditional)
		goto free;

	merge_slist(&cond_true, cond_false);

	FOR_EACH_PTR(cond_true, sm) {
		__set_state(sm);
	} END_FOR_EACH_PTR(sm);
	free_slist(&cond_true);
	free_slist(&cond_false);
free:
	free_string(var_name);

}

void __match_initializer_call(struct symbol *sym)
{
	struct call_back_list *call_backs;
	struct expression *initializer = sym->initializer;
	struct expression *e_assign, *e_symbol;
	const char *fn;

	if (initializer->fn->type != EXPR_SYMBOL
	    || !initializer->fn->symbol)
		return;
	fn = initializer->fn->symbol->ident->name;
	call_backs = get_call_backs(fn);
	if (!call_backs)
		return;

	e_assign = alloc_expression(initializer->pos, EXPR_ASSIGNMENT);
	e_symbol = alloc_expression(initializer->pos, EXPR_SYMBOL);
	e_symbol->symbol = sym;
	e_symbol->symbol_name = sym->ident;
	e_assign->left = e_symbol;
	e_assign->right = initializer;
	call_call_backs(call_backs, ASSIGN_CALL, fn, e_assign);
	assign_condition_funcs(fn, e_assign, call_backs);
}

static void match_assign_call(struct expression *expr)
{
	struct call_back_list *call_backs;
	const char *fn;

	if (expr->right->fn->type != EXPR_SYMBOL || !expr->right->fn->symbol)
		return;
	fn = expr->right->fn->symbol->ident->name;
	call_backs = get_call_backs(fn);
	if (!call_backs)
		return;
	call_call_backs(call_backs, ASSIGN_CALL, fn, expr);
	assign_condition_funcs(fn, expr, call_backs);
}

static void match_conditional_call(struct expression *expr)
{
	struct call_back_list *call_backs;
	struct fcall_back *tmp;
	struct sm_state *sm;
	const char *fn;

	if (expr->type != EXPR_CALL)
		return;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	fn = expr->fn->symbol->ident->name;
	call_backs = get_call_backs(fn);
	if (!call_backs)
		return;
	in_hook = 1;
	FOR_EACH_PTR(call_backs, tmp) {
		if (tmp->type != CONDITIONAL_CALL)
			continue;

		(tmp->call_back)(fn, expr, tmp->info);

		FOR_EACH_PTR(cond_true, sm) {
			__set_true_false_sm(sm, NULL);
		} END_FOR_EACH_PTR(sm);
		free_slist(&cond_true);

		FOR_EACH_PTR(cond_false, sm) {
			__set_true_false_sm(NULL, sm);
		} END_FOR_EACH_PTR(sm);
		free_slist(&cond_false);

	} END_FOR_EACH_PTR(tmp);
	in_hook = 0;
}

void set_cond_states(const char *name, int owner, struct symbol *sym, 
		     struct smatch_state *true_state,
		     struct smatch_state *false_state)
{
	if (!in_hook) {
		printf("Error:  call set_true_false_states() not"
		       "set_cond_states()\n");
		return;
	}

	if (debug_states) {
		struct smatch_state *tmp;

		tmp = get_state(name, owner, sym);
		SM_DEBUG("%d set_true_false '%s'.  Was %s.  Now T:%s F:%s\n",
			 get_lineno(), name, show_state(tmp),
			 show_state(true_state), show_state(false_state));
	}

	if (true_state) {
		set_state_slist(&cond_true, name, owner, sym, true_state);
	}
	if (false_state)
		set_state_slist(&cond_false, name, owner, sym, false_state);
}

void create_function_hash(void)
{
	hcreate_r(10000, &func_hash);  // Apparently 1000 is too few...
}

void register_function_hooks(int id)
{
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign_call, CALL_ASSIGNMENT_HOOK);
	add_hook(&match_conditional_call, CONDITION_HOOK);
}
