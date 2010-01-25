/*
 * sparse/smatch_modification_hooks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_function_hashtable.h"

struct mcall_back {
	int owner;
	modification_hook *call_back;
	void *info;
};

static modification_hook **default_hooks;

ALLOCATOR(mcall_back, "modification call backs");
DECLARE_PTR_LIST(mod_cb_list, struct mcall_back);

DEFINE_FUNCTION_HASHTABLE_STATIC(mcall, struct mcall_back, struct mod_cb_list);
static struct hashtable *var_hash;

static struct mcall_back *alloc_mcall_back(int owner, modification_hook *call_back,
					   void *info)
{
	struct mcall_back *cb;

	cb = __alloc_mcall_back(0);
	cb->owner = owner;
	cb->call_back = call_back;
	cb->info = info;
	return cb;
}

static int is_duplicate(int owner, const char *variable)
{
	struct mod_cb_list *call_backs;
	struct mcall_back *cb;

	call_backs = search_mcall(var_hash, (char *)variable);
	FOR_EACH_PTR(call_backs, cb) {
		if (cb->owner == owner)
			return 1;
	} END_FOR_EACH_PTR(cb);
	return 0;
}

void add_modification_hook(int owner, const char *variable, modification_hook *call_back, void *info)
{
	struct mcall_back *cb;

	if (is_duplicate(owner, variable))
		return;
	cb = alloc_mcall_back(owner, call_back, info);
	add_mcall(var_hash, variable, cb);
}

void add_modification_hook_expr(int owner, struct expression *expr, modification_hook *call_back, void *info)
{
	struct mcall_back *cb;
	char *name;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return;
	if (is_duplicate(owner, name))
		return;
	cb = alloc_mcall_back(owner, call_back, info);
	add_mcall(var_hash, name, cb);
	free_string(name);
}

void set_default_modification_hook(int owner, modification_hook *call_back)
{
	default_hooks[owner] = call_back;
}

void __use_default_modification_hook(int owner, const char *variable)
{
	if (owner < 0 || owner >= num_checks)
		return;
	if (!default_hooks[owner])
		return;
	add_modification_hook(owner, variable, default_hooks[owner], NULL);
}

static void call_call_backs(struct mod_cb_list *list,
			const char *variable,
			struct symbol *sym,
			struct expression *expr)
{
	struct mcall_back *tmp;

	FOR_EACH_PTR(list, tmp) {
		(tmp->call_back)(variable, sym, expr, tmp->info);
	} END_FOR_EACH_PTR(tmp);
}


static void match_assign(struct expression *expr)
{
	struct mod_cb_list *call_backs;
	struct expression *left;
	struct symbol *sym;
	char *name;

	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name)
		return;
	call_backs = search_mcall(var_hash, name);
	if (!call_backs)
		goto free;
	call_call_backs(call_backs, name, sym, expr);
free:
	free_string(name);
}

static void unop_expr(struct expression *expr)
{
	struct mod_cb_list *call_backs;
	struct symbol *sym;
	char *name = NULL;

	if (expr->op != SPECIAL_DECREMENT && expr->op != SPECIAL_INCREMENT)
		return;

	expr = strip_expr(expr->unop);
	name = get_variable_from_expr(expr, &sym);
	if (!name)
		goto free;
	call_backs = search_mcall(var_hash, name);
	if (!call_backs)
		goto free;
	call_call_backs(call_backs, name, sym, expr);
free:
	free_string(name);
}

static void match_call(struct expression *expr)
{
	struct mod_cb_list *call_backs;
	struct expression *arg;
	struct symbol *sym;
	char *name;

	FOR_EACH_PTR(expr->args, arg) {
		arg = strip_expr(arg);
		if (arg->type != EXPR_PREOP || arg->op != '&')
			continue;
		arg = strip_expr(arg->unop);
		name = get_variable_from_expr(arg, &sym);
		if (!name)
			continue;
		call_backs = search_mcall(var_hash, name);
		if (call_backs)
			call_call_backs(call_backs, name, sym, expr);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

static void match_end_func(struct symbol *sym)
{
	destroy_function_hashtable(var_hash);
	var_hash = create_function_hashtable(100);
}

void register_modification_hooks(int id)
{
	int i;

	var_hash = create_function_hashtable(100);
	default_hooks = malloc(num_checks * sizeof(*default_hooks));
	for (i = 0; i < num_checks; i++)
		default_hooks[i] = NULL;
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
