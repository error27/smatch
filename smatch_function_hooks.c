#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"

struct fcall_back {
	void *call_back;
	void *data;
};

ALLOCATOR(fcall_back, "call backs");
DECLARE_PTR_LIST(call_back_list, struct fcall_back);

void add_function_hook(const char *look_for, void *call_back, void *data)
{
	ENTRY e, *ep;
	struct fcall_back *cb;

	cb = __alloc_fcall_back(0);
	cb->call_back = call_back;
	cb->data = data;
	e.key = alloc_string(look_for);
	ep = hsearch(e, FIND);
	if (!ep) {
		struct call_back_list *list = NULL;
		
		add_ptr_list(&list, cb);
		e.data = list;
	} else {
		free_string(ep->key);
		add_ptr_list((struct call_back_list **)&ep->data, cb);
		e.data = ep->data;
	}
	hsearch(e, ENTER);
}

typedef void (call_back_func)(struct expression *expr, void *data);
static void match_function_call(struct expression *expr)
{
	ENTRY e, *ep;
	struct fcall_back *tmp;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	e.key = expr->fn->symbol->ident->name;
	ep = hsearch(e, FIND);
	if (!ep)
		return;

	FOR_EACH_PTR((struct call_back_list *)ep->data, tmp) {
		((call_back_func *) tmp->call_back)(expr, tmp->data);
	} END_FOR_EACH_PTR(tmp);
}

void register_function_hooks(int id)
{
	hcreate(1000);  // We will track maybe 1000 functions.
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
}
