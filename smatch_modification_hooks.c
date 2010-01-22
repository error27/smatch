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
#include "cwchash/hashtable.h"

struct mcall_back {
	modification_hook *call_back;
	void *info;
};

ALLOCATOR(mcall_back, "modification call backs");
DECLARE_PTR_LIST(mod_cb_list, struct mcall_back);

static struct hashtable *var_hash;

static unsigned int
djb2_hash(void *ky)
{
	char *str = (char *)ky;
	unsigned long hash = 5381;
        int c;

        while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

        return hash;
}

static int
equalkeys(void *k1, void *k2)
{
	return !strcmp((char *)k1, (char *)k2);
}

DEFINE_HASHTABLE_INSERT(insert_cb_list, char, struct mod_cb_list);
DEFINE_HASHTABLE_SEARCH(search_cb_list, char, struct mod_cb_list);
DEFINE_HASHTABLE_REMOVE(remove_cb_list, char, struct mod_cb_list);

static struct mcall_back *alloc_mcall_back(modification_hook *call_back,
					   void *info)
{
	struct mcall_back *cb;

	cb = __alloc_mcall_back(0);
	cb->call_back = call_back;
	cb->info = info;
	return cb;
}

static struct mod_cb_list *get_mcall_backs(const char *look_for)
{
	return search_cb_list(var_hash, (char *)look_for);
}

static void add_mcall_back(const char *look_for, struct mcall_back *cb)
{
	struct mod_cb_list *list;
	char *key;

	key = alloc_string(look_for);
	list = search_cb_list(var_hash, key);
	if (!list) {
		add_ptr_list(&list, cb);
	} else {
		remove_cb_list(var_hash, key);
		add_ptr_list(&list, cb);
	}
	insert_cb_list(var_hash, key, list);
}

void add_modification_hook(const char *variable, modification_hook *call_back, void *info)
{
	struct mcall_back *cb;

	cb = alloc_mcall_back(call_back, info);
	add_mcall_back(variable, cb);
}

void add_modification_hook_expr(struct expression *expr, modification_hook *call_back, void *info)
{
	struct mcall_back *cb;
	char *name;

	expr = strip_expr(expr);
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return;
	cb = alloc_mcall_back(call_back, info);
	add_mcall_back(name, cb);
	free_string(name);
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
	call_backs = get_mcall_backs(name);
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
	call_backs = get_mcall_backs(name);
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
		call_backs = get_mcall_backs(name);
		if (call_backs)
			call_call_backs(call_backs, name, sym, expr);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

static void match_end_func(struct symbol *sym)
{
	hashtable_destroy(var_hash, 0);
	var_hash = create_hashtable(100, djb2_hash, equalkeys);
}

void register_modification_hooks(int id)
{
	var_hash = create_hashtable(100, djb2_hash, equalkeys);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
