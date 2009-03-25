#define _GNU_SOURCE
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"

ALLOCATOR(fcall_back, "call backs");

static struct hsearch_data func_hash;

void add_function_hook(const char *look_for, func_hook *call_back, void *data)
{
	ENTRY e, *ep;
	struct fcall_back *cb;

	cb = __alloc_fcall_back(0);
	cb->call_back = call_back;
	cb->data = data;
	e.key = alloc_string(look_for);
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep) {
		struct call_back_list *list = NULL;
		
		add_ptr_list(&list, cb);
		e.data = list;
	} else {
		free_string(ep->key);
		add_ptr_list((struct call_back_list **)&ep->data, cb);
		e.data = ep->data;
	}
	hsearch_r(e, ENTER, &ep, &func_hash);
}

static void match_function_call(struct expression *expr)
{
	ENTRY e, *ep;
	struct fcall_back *tmp;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	e.key = expr->fn->symbol->ident->name;
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep)
		return;

	FOR_EACH_PTR((struct call_back_list *)ep->data, tmp) {
		(tmp->call_back)(e.key, expr, tmp->data);
	} END_FOR_EACH_PTR(tmp);
}

void register_function_hooks(int id)
{
	hcreate_r(1000, &func_hash);  // We will track maybe 1000 functions.
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
}
