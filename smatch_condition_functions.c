#define _GNU_SOURCE
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_slist.h"

static struct state_list *cond_true = NULL;
static struct state_list *cond_false = NULL;
static struct hsearch_data func_hash;
static int in_hook = 0;

void add_conditional_hook(const char *look_for, func_hook *call_back,
			  void *data)
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

static void match_conditional_call(struct expression *expr)
{
	ENTRY e, *ep;
	struct fcall_back *tmp;
	struct sm_state *sm;

	if (expr->type != EXPR_CALL)
		return;

	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol)
		return;

	e.key = expr->fn->symbol->ident->name;
	hsearch_r(e, FIND, &ep, &func_hash);
	if (!ep)
		return;
	in_hook = 1;
	FOR_EACH_PTR((struct call_back_list *)ep->data, tmp) {
		(tmp->call_back)(e.key, expr, tmp->data);

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

void register_conditional_function_hooks(int id)
{
	hcreate_r(500, &func_hash);
	add_hook(&match_conditional_call, CONDITION_HOOK);
}
