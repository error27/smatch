/*
 * Copyright (C) 2015 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static bool is_non_null_array(struct expression *expr)
{
	struct symbol *type;
	struct symbol *sym;
	struct symbol *tmp;
	char *name;
	int i;

	type = get_type(expr);
	if (!type || type->type != SYM_ARRAY)
		return 0;
	if (expr->type == EXPR_SYMBOL)
		return 1;
	if (implied_not_equal(expr, 0))
		return 1;

	/* verify that it's not the first member of the struct */
	if (expr->type != EXPR_DEREF || !expr->member)
		return 0;
	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!name || !sym)
		return 0;
	type = get_real_base_type(sym);
	if (!type || type->type != SYM_PTR)
		return 0;
	type = get_real_base_type(type);
	if (type->type != SYM_STRUCT)
		return 0;

	i = 0;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		i++;
		if (!tmp->ident)
			continue;
		if (strcmp(expr->member->name, tmp->ident->name) == 0) {
			if (i == 1)
				return 0;
			return 1;
		}
	} END_FOR_EACH_PTR(tmp);

	return 0;
}

int get_member_offset(struct symbol *type, char *member_name)
{
	struct symbol *tmp;
	int offset;

	if (type->type != SYM_STRUCT)
		return -1;

	offset = 0;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (!type->ctype.attribute->is_packed)
			offset = ALIGN(offset, tmp->ctype.alignment);
		if (tmp->ident && tmp->ident->name &&
		    strcmp(member_name, tmp->ident->name) == 0) {
			return offset;
		}
		offset += type_bytes(tmp);
	} END_FOR_EACH_PTR(tmp);
	return -1;
}

int get_member_offset_from_deref(struct expression *expr)
{
	struct symbol *type;
	struct ident *member;
	int offset;

	if (expr->type != EXPR_DEREF)  /* hopefully, this doesn't happen */
		return -1;

	if (expr->member_offset >= 0)
		return expr->member_offset;

	member = expr->member;
	if (!member || !member->name)
		return -1;

	type = get_type(expr->deref);
	if (!type || type->type != SYM_STRUCT)
		return -1;

	offset = get_member_offset(type, member->name);
	if (offset >= 0)
		expr->member_offset = offset;
	return offset;
}

static void add_offset_to_min(struct range_list **rl, int offset)
{
	sval_t sval, max;
	struct range_list *orig = *rl;
	struct range_list *offset_rl;
	struct range_list *big_rl;
	struct range_list *tmp;

	/*
	 * I don't know.  I guess I want to preserve the upper value because
	 * that has no information.  Only the lower value is interesting.
	 */

	if (!orig)
		return;
	sval = rl_min(orig); /* get the type */
	sval.value = offset;

	offset_rl = alloc_rl(sval, sval);
	tmp = rl_binop(orig, '+', offset_rl);

	max = rl_max(orig);
	/* if we actually "know" the max then preserve it. */
	if (max.value < 100000) {
		*rl = tmp;
		return;
	}
	sval.value = 0;
	big_rl = alloc_rl(sval, max);

	*rl = rl_intersection(tmp, big_rl);
}

static struct range_list *where_allocated_rl(struct symbol *sym)
{
	if (sym->ctype.modifiers & (MOD_TOPLEVEL | MOD_STATIC)) {
		if (sym->initializer)
			return alloc_rl(data_seg_min, data_seg_max);
		else
			return alloc_rl(bss_seg_min, bss_seg_max);
	}
	return alloc_rl(stack_seg_min, stack_seg_max);
}

int get_address_rl(struct expression *expr, struct range_list **rl)
{
	expr = strip_expr(expr);
	if (!expr)
		return 0;

	if (expr->type == EXPR_STRING) {
		*rl = alloc_rl(text_seg_min, text_seg_max);
		return 1;
	}

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		struct expression *unop;

		unop = strip_expr(expr->unop);
		if (unop->type == EXPR_SYMBOL) {
			*rl = where_allocated_rl(unop->symbol);
			return 1;
		}

		if (unop->type == EXPR_DEREF) {
			int offset = get_member_offset_from_deref(unop);

			unop = strip_expr(unop->unop);
			if (unop->type == EXPR_SYMBOL) {
				*rl = where_allocated_rl(unop->symbol);
			} else if (unop->type == EXPR_PREOP && unop->op == '*') {
				unop = strip_expr(unop->unop);
				get_absolute_rl(unop, rl);
			} else {
				return 0;
			}

			add_offset_to_min(rl, offset);
			return 1;
		}

		return 0;
	}

	if (is_non_null_array(expr)) {
		*rl = alloc_rl(array_min_sval, array_max_sval);
		return 1;
	}

	return 0;
}
