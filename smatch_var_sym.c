/*
 * sparse/smatch_var_sym.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

ALLOCATOR(var_sym, "var_sym structs");

struct var_sym *alloc_var_sym(const char *var, struct symbol *sym)
{
	struct var_sym *tmp;

	tmp = __alloc_var_sym(0);
	tmp->var = alloc_string(var);
	tmp->sym = sym;
	return tmp;
}

struct var_sym_list *expr_to_vsl(struct expression *expr)
{
	struct var_sym_list *ret = NULL;
	char *var;
	struct symbol *sym;

	expr = strip_expr(expr);
	if (!expr)
		return NULL;

	if (expr->type == EXPR_BINOP ||
	    expr->type == EXPR_LOGICAL ||
	    expr->type == EXPR_COMPARE) {
		struct var_sym_list *left, *right;

		left = expr_to_vsl(expr->left);
		right = expr_to_vsl(expr->right);
		ret = combine_var_sym_lists(left, right);
		free_var_syms_and_list(&left);
		free_var_syms_and_list(&right);
		return ret;
	}
	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym) {
		free_string(var);
		return NULL;
	}
	add_var_sym(&ret, var, sym);
	return ret;
}

void add_var_sym(struct var_sym_list **list, const char *var, struct symbol *sym)
{
	struct var_sym *tmp;

	if (in_var_sym_list(*list, var, sym))
		return;
	tmp = alloc_var_sym(var, sym);
	add_ptr_list(list, tmp);
}

void add_var_sym_expr(struct var_sym_list **list, struct expression *expr)
{
	char *var;
	struct symbol *sym;

	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto free;
	add_var_sym(list, var, sym);
free:
	free_string(var);
}

static void free_var_sym(struct var_sym *vs)
{
	free_string(vs->var);
	__free_var_sym(vs);
}

void del_var_sym(struct var_sym_list **list, const char *var, struct symbol *sym)
{
	struct var_sym *tmp;

	FOR_EACH_PTR(*list, tmp) {
		if (tmp->sym == sym && strcmp(tmp->var, var) == 0) {
			DELETE_CURRENT_PTR(tmp);
			free_var_sym(tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
}

int in_var_sym_list(struct var_sym_list *list, const char *var, struct symbol *sym)
{
	struct var_sym *tmp;

	FOR_EACH_PTR(list, tmp) {
		if (tmp->sym == sym && strcmp(tmp->var, var) == 0)
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

struct var_sym_list *clone_var_sym_list(struct var_sym_list *from_vsl)
{
	struct var_sym *tmp, *clone_vs;
	struct var_sym_list *to_vsl = NULL;

	FOR_EACH_PTR(from_vsl, tmp) {
		clone_vs = alloc_var_sym(tmp->var, tmp->sym);
		add_ptr_list(&to_vsl, clone_vs);
	} END_FOR_EACH_PTR(tmp);
	return to_vsl;
}

void merge_var_sym_list(struct var_sym_list **dest, struct var_sym_list *src)
{
	struct var_sym *tmp;

	FOR_EACH_PTR(src, tmp) {
		add_var_sym(dest, tmp->var, tmp->sym);
	} END_FOR_EACH_PTR(tmp);
}

struct var_sym_list *combine_var_sym_lists(struct var_sym_list *one, struct var_sym_list *two)
{
	struct var_sym_list *to_vsl;

	to_vsl = clone_var_sym_list(one);
	merge_var_sym_list(&to_vsl, two);
	return to_vsl;
}

void free_var_sym_list(struct var_sym_list **list)
{
	__free_ptr_list((struct ptr_list **)list);
}

void free_var_syms_and_list(struct var_sym_list **list)
{
	struct var_sym *tmp;

	FOR_EACH_PTR(*list, tmp) {
		free_var_sym(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_var_sym_list(list);
}

