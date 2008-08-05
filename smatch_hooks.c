/*
 * sparse/smatch_hooks.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

struct hook_container {
	int hook_type;
	int data_type;
	void * fn;
};
ALLOCATOR(hook_container, "hook functions");
DECLARE_PTR_LIST(hook_func_list, struct hook_container);
static struct hook_func_list *hook_funcs;
static struct hook_func_list *merge_funcs;

void add_hook(void *func, enum hook_type type)
{
	struct hook_container *container = __alloc_hook_container(0);
	container->hook_type = type;
	container->fn = func;
	switch(type) {
	case EXPR_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case STMT_HOOK:
		container->data_type = STMT_HOOK;
		break;
	case SYM_HOOK:
		container->data_type = SYM_HOOK;
		break;
	case DECLARATION_HOOK:
		container->data_type = SYM_HOOK;
		break;
	case ASSIGNMENT_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case ASSIGNMENT_AFTER_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case CONDITION_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case WHOLE_CONDITION_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case FUNCTION_CALL_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case FUNCTION_CALL_AFTER_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case DEREF_HOOK:
		container->data_type = EXPR_HOOK;
		break;
	case BASE_HOOK:
		container->data_type = SYM_HOOK;
		break;
	case FUNC_DEF_HOOK:
		container->data_type = SYM_HOOK;
		break;
	case END_FUNC_HOOK:
		container->data_type = SYM_HOOK;
		break;
	case RETURN_HOOK:
		container->data_type = STMT_HOOK;
		break;
	}
	add_ptr_list(&hook_funcs, container);
}

void add_merge_hook(int client_id, merge_func_t *func)
{
	struct hook_container *container = __alloc_hook_container(0);
	container->data_type = client_id;
	container->fn = func;
	add_ptr_list(&merge_funcs, container);
}


static void pass_expr_to_client(void * fn, void * data)
{
	typedef void (expr_func)(struct expression *expr);
	((expr_func *) fn)((struct expression *) data);
}

static void pass_stmt_to_client(void * fn, void * data)
{
	typedef void (stmt_func)(struct statement *stmt);
	((stmt_func *) fn)((struct statement *) data);
}

static void pass_sym_to_client(void * fn, void * data)
{
	typedef void (sym_func)(struct symbol *sym);
	((sym_func *) fn)((struct symbol *) data);
}

void __pass_to_client(void *data, enum hook_type type)
{
	struct hook_container *container;

	if (!data)
		return;

	FOR_EACH_PTR(hook_funcs, container) {
		if (container->hook_type == type) {
			switch(container->data_type) {
			case EXPR_HOOK:
				pass_expr_to_client(container->fn, data);
				break;
			case STMT_HOOK:
				pass_stmt_to_client(container->fn, data);
				break;
			case SYM_HOOK:
				pass_sym_to_client(container->fn, data);
				break;
			}
		}
	} END_FOR_EACH_PTR(container);
}

void __pass_declarations_to_client(struct symbol_list *sym_list)
{
	struct symbol *sym;
	FOR_EACH_PTR(sym_list, sym) {
		__pass_to_client(sym, DECLARATION_HOOK);
	} END_FOR_EACH_PTR(sym);
}

int __has_merge_function(int client_id)
{
	struct hook_container *tmp;

	FOR_EACH_PTR(merge_funcs, tmp) {
		if (tmp->data_type == client_id)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

int __client_merge_function(int owner, const char *name, struct symbol *sym, 
			    int s1, int s2)
{
	struct hook_container *tmp;

	FOR_EACH_PTR(merge_funcs, tmp) {
		if (tmp->data_type == owner)
			return ((merge_func_t *) tmp->fn)(name, sym, s1, s2);
	} END_FOR_EACH_PTR(tmp);
	return UNDEFINED;
}
