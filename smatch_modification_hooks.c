/*
 * sparse/smatch_modification_hooks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#define _GNU_SOURCE
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"

struct mcall_back {
	modification_hook *call_back;
	void *info;
};

ALLOCATOR(mcall_back, "modification call backs");
DECLARE_PTR_LIST(mod_cb_list, struct mcall_back);

static struct hsearch_data var_hash;

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
	ENTRY e, *ep;

	e.key = (char *)look_for;
	hsearch_r(e, FIND, &ep, &var_hash);
	if (!ep)
		return NULL;
	return (struct mod_cb_list *)ep->data;
}

static void add_mcall_back(const char *look_for, struct mcall_back *cb)
{
	ENTRY e, *ep;
	char *old_key = NULL;

	e.key = alloc_string(look_for);
	hsearch_r(e, FIND, &ep, &var_hash);
	if (!ep) {
		struct mod_cb_list *list = NULL;
		
		add_ptr_list(&list, cb);
		e.data = list;
	} else {
		old_key = e.key;
		e.key = ep->key;
		add_ptr_list((struct mod_cb_list **)&ep->data, cb);
		e.data = ep->data;
	}
	if (!hsearch_r(e, ENTER, &ep, &var_hash)) {
		printf("Error hash table too small in smatch_modification_hooks.c\n");
		exit(1);
	}
	free_string(old_key);
}

void add_modification_hook(const char *variable, modification_hook *call_back, void *info)
{
	struct mcall_back *cb;

	cb = alloc_mcall_back(call_back, info);
	add_mcall_back(variable, cb);
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

static void match_end_func(struct symbol *sym)
{
	hdestroy_r(&var_hash);
	hcreate_r(1000, &var_hash);
}

void register_modification_hooks(int id)
{
	hcreate_r(1000, &var_hash);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}

