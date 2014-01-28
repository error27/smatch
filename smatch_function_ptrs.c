/*
 * smatch/smatch_function_ptrs.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Track how functions are saved as various struct members or passed as
 * parameters.
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static char *get_array_ptr(struct expression *expr)
{
	struct expression *array;
	struct symbol *type;
	char *name;
	char buf[256];

	array = get_array_name(expr);
	/* FIXME:  is_array() should probably be is_array_element() */
	type = get_type(expr);
	if (!array && type && type->type == SYM_ARRAY)
		array = expr;
	if (array) {
		name = expr_to_var(array);
		if (!name)
			return NULL;
		snprintf(buf, sizeof(buf), "%s[]", name);
		return alloc_string(buf);
	}

	expr = get_assigned_expr(expr);
	array = get_array_name(expr);
	if (!array)
		return NULL;
	name = expr_to_var(array);
	if (!name)
		return NULL;
	snprintf(buf, sizeof(buf), "%s[]", name);
	free_string(name);
	return alloc_string(buf);
}

static int is_local_symbol(struct symbol *sym)
{
	if (!sym || !sym->scope || !sym->scope->token)
		return 0;
	if (positions_eq(sym->scope->token->pos, cur_func_sym->pos))
		return 1;
	return 0;
}

static char *ptr_prefix(struct symbol *sym)
{
	static char buf[128];


	if (is_local_symbol(sym))
		snprintf(buf, sizeof(buf), "%s ptr", get_function());
	else if (sym && toplevel(sym->scope))
		snprintf(buf, sizeof(buf), "%s ptr", get_base_file());
	else
		snprintf(buf, sizeof(buf), "ptr");

	return buf;
}

char *get_fnptr_name(struct expression *expr)
{
	char *name;

	expr = strip_expr(expr);

	/* (*ptrs[0])(a, b, c) is the same as ptrs[0](a, b, c); */
	if (expr->type == EXPR_PREOP && expr->op == '*') {
		struct expression *unop = strip_expr(expr->unop);

		if (unop->type == EXPR_PREOP && unop->op == '*')
			expr = unop;
		else if (unop->type == EXPR_SYMBOL)
			expr = unop;
	}

	name = get_array_ptr(expr);
	if (name)
		return name;

	if (expr->type == EXPR_SYMBOL) {
		int param;
		char buf[256];
		struct symbol *sym;
		struct symbol *type;

		param = get_param_num_from_sym(expr->symbol);
		if (param >= 0) {
			snprintf(buf, sizeof(buf), "%s param %d", get_function(), param);
			return alloc_string(buf);
		}

		name =  expr_to_var_sym(expr, &sym);
		if (!name)
			return NULL;
		type = get_type(expr);
		if (type && type->type == SYM_PTR) {
			snprintf(buf, sizeof(buf), "%s %s", ptr_prefix(sym), name);
			free_string(name);
			return alloc_string(buf);
		}
		return name;
	}
	name = get_member_name(expr);
	if (name)
		return name;
	return expr_to_var(expr);
}

static void match_passes_function_pointer(struct expression *expr)
{
	struct expression *arg, *tmp;
	struct symbol *type;
	char *called_name;
	char *fn_name;
	char ptr_name[256];
	int i;


	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;

		tmp = strip_expr(arg);
		if (tmp->type == EXPR_PREOP && tmp->op == '&')
			tmp = strip_expr(tmp->unop);

		type = get_type(tmp);
		if (type && type->type == SYM_PTR)
			type = get_real_base_type(type);
		if (!type || type->type != SYM_FN)
			continue;

		called_name = expr_to_var(expr->fn);
		if (!called_name)
			return;
		fn_name = get_fnptr_name(tmp);
		if (!fn_name)
			goto free;

		snprintf(ptr_name, sizeof(ptr_name), "%s param %d", called_name, i);
		sql_insert_function_ptr(fn_name, ptr_name);
free:
		free_string(fn_name);
		free_string(called_name);
	} END_FOR_EACH_PTR(arg);

}

static void match_function_assign(struct expression *expr)
{
	struct expression *right;
	struct symbol *sym;
	char *fn_name;
	char *ptr_name;

	if (__in_fake_assign)
		return;

	right = strip_expr(expr->right);
	if (right->type == EXPR_PREOP && right->op == '&')
		right = strip_expr(right->unop);
	if (right->type != EXPR_SYMBOL && right->type != EXPR_DEREF)
		return;
	sym = get_type(right);
	if (!sym)
		return;
	if (sym->type != SYM_FN && sym->type != SYM_PTR && sym->type != SYM_ARRAY)
		return;
	if (sym->type == SYM_PTR) {
		sym = get_real_base_type(sym);
		if (!sym)
			return;
		if (sym->type != SYM_FN && sym != &void_ctype)
			return;
	}

	fn_name = get_fnptr_name(right);
	ptr_name = get_fnptr_name(expr->left);
	if (!fn_name || !ptr_name)
		goto free;
	if (strcmp(fn_name, ptr_name) == 0)
		goto free;

	sql_insert_function_ptr(fn_name, ptr_name);

free:
	free_string(fn_name);
	free_string(ptr_name);
}

void register_function_ptrs(int id)
{
	my_id = id;

	if (!option_info)
		return;

	add_hook(&match_passes_function_pointer, FUNCTION_CALL_HOOK);
	add_hook(&match_function_assign, ASSIGNMENT_HOOK);
	add_hook(&match_function_assign, GLOBAL_ASSIGNMENT_HOOK);
}
