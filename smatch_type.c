/*
 * sparse/smatch_types.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The idea here is that you have an expression and you
 * want to know what the type is for that.
 */

#include "smatch.h"

static struct symbol *get_type_symbol(struct expression *expr)
{
	if (!expr || expr->type != EXPR_SYMBOL)
		return NULL;

	return get_base_type(expr->symbol);
}

static struct symbol *get_symbol_from_deref(struct expression *expr)
{
	struct ident *member;
	struct symbol *struct_sym;
	struct symbol *tmp;

	if (!expr || expr->type != EXPR_DEREF)
		return NULL;

	member = expr->member;
	struct_sym = get_type(expr->deref);
	if (!struct_sym) {
//		sm_msg("could not find struct type");
		return NULL;
	}
	if (struct_sym->type == SYM_PTR)
		struct_sym = get_base_type(struct_sym);
	FOR_EACH_PTR(struct_sym->symbol_list, tmp) {
		if (tmp->ident == member)
			return get_base_type(tmp);
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

static struct symbol *get_return_type(struct expression *expr)
{
	struct symbol *tmp;

	tmp = get_type(expr->fn);
	if (!tmp)
		return NULL;
	return get_base_type(tmp);
}

static struct symbol *fake_pointer_sym(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *base;

	sym = alloc_symbol(expr->pos, SYM_PTR);
	expr = expr->unop;
	base = get_type(expr);
	if (!base)
		return NULL;
	sym->ctype.base_type = base;
	return sym;
}

struct symbol *get_type(struct expression *expr)
{
	struct symbol *tmp;

	if (!expr)
		return NULL;
	expr = strip_parens(expr);

	switch (expr->type) {
	case EXPR_SYMBOL:
		return get_type_symbol(expr);
	case EXPR_DEREF:
		return get_symbol_from_deref(expr);
	case EXPR_PREOP:
		if (expr->op == '&')
			return fake_pointer_sym(expr);
		return get_type(expr->unop);
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		return get_base_type(expr->cast_type);
	case EXPR_BINOP:
		if (expr->op != '+')
			return NULL;
		tmp = get_type(expr->left);
		if (!tmp || (tmp->type != SYM_ARRAY && tmp->type != SYM_PTR))
			return NULL;
		return get_base_type(tmp);
	case EXPR_CALL:
		return get_return_type(expr);
//	default:
//		sm_msg("unhandled type %d", expr->type);
	}


	return NULL;
}
