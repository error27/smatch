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

struct symbol *get_ptr_type_ptr(struct symbol *sym)
{
	if (!sym) {
		return NULL;
	}
	
	if (sym->type != SYM_NODE)
		return NULL;
	sym = get_base_type(sym);
	if (sym->type != SYM_PTR)
		return NULL;
	sym = get_base_type(sym);
	return sym;
}

static struct symbol *get_struct_sym(struct expression *expr)
{
	struct symbol *base_type;
	struct symbol *parent_struct;
	struct symbol *tmp;

	if (expr->type != EXPR_PREOP)
		return NULL;

	expr = expr->unop;
	if (expr->type == EXPR_DEREF) {
		parent_struct = get_struct_sym(expr->deref);
		if (!parent_struct)
			return NULL;
		tmp = NULL;
		FOR_EACH_PTR(parent_struct->symbol_list, tmp) {
			if (tmp->ident == expr->member)
				break;
		} END_FOR_EACH_PTR(tmp);
		if (!tmp || tmp->ident != expr->member)
			return NULL;
		base_type = get_base_type(tmp);
	} else if (expr->type == EXPR_SYMBOL) {
		base_type = get_base_type(expr->symbol);
	} else {
		return NULL;
	}
	if (base_type->type != SYM_PTR)
		return NULL;
	base_type = get_base_type(base_type);
	if (base_type->type != SYM_STRUCT && base_type->type != SYM_UNION)
		return NULL;
	return base_type;
}

static struct symbol *get_deref_type(struct expression *expr)
{
	struct ident *member;
	struct symbol *struct_sym;
	struct symbol *tmp;

	if (!expr || expr->type != EXPR_DEREF)
		return NULL;

	member = expr->member;
	struct_sym = get_struct_sym(expr->deref);
	if (!struct_sym || (struct_sym->type != SYM_STRUCT 
			    && struct_sym->type != SYM_UNION))
		return NULL;
	FOR_EACH_PTR(struct_sym->symbol_list, tmp) {
		if (tmp->ident == member)
			return get_ptr_type_ptr(tmp);
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

struct symbol *get_ptr_type(struct expression *expr)
{
	struct symbol *ptr_type = NULL;

	if (!expr)
		return NULL;
	if (expr->type == EXPR_DEREF)
		ptr_type = get_deref_type(expr);
	if (expr->type == EXPR_SYMBOL)
		ptr_type = get_ptr_type_ptr(expr->symbol);
	return ptr_type;
}

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
		struct_sym =  get_base_type(struct_sym);
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

struct symbol *get_type(struct expression *expr)
{
	struct symbol *tmp;

	if (!expr)
		return NULL;
	expr = strip_expr(expr);

	switch(expr->type) {
	case EXPR_SYMBOL:
		return get_type_symbol(expr);
	case EXPR_DEREF:
		return get_symbol_from_deref(expr);
	case EXPR_PREOP:
		return get_type(expr->unop);
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
