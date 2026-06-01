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

static bool matches_anonymous_union(struct symbol *sym, const char *member_name)
{
	struct symbol *type, *tmp;

	if (sym->ident)
		return false;
	type = get_real_base_type(sym);
	if (!type || type->type != SYM_UNION)
		return false;

	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (tmp->ident &&
		    strcmp(member_name, tmp->ident->name) == 0) {
			return true;
		}
	} END_FOR_EACH_PTR(tmp);

	return false;
}

int get_member_offset(struct symbol *type, const char *member_name)
{
	struct symbol *tmp;
	int offset;
	int bits;

	if (!type || type->type != SYM_STRUCT)
		return -1;

	bits = 0;
	offset = 0;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (bits_to_bytes(bits + type_bits(tmp)) > tmp->ctype.alignment) {
			offset += bits_to_bytes(bits);
			bits = 0;
		}
		offset = ALIGN(offset, tmp->ctype.alignment);
		if (tmp->ident &&
		    strcmp(member_name, tmp->ident->name) == 0) {
			return offset;
		}
		if (matches_anonymous_union(tmp, member_name))
			return offset;
		if (!(type_bits(tmp) % 8) && type_bits(tmp) / 8 == type_bytes(tmp))
			offset += type_bytes(tmp);
		else
			bits += type_bits(tmp);
	} END_FOR_EACH_PTR(tmp);
	return -1;
}

int get_member_offset_from_deref(struct expression *expr)
{
	struct symbol *type;
	struct ident *member;
	int offset;

	/*
	 * FIXME: This doesn't handle foo.u.bar correctly.
	 *
	 */

	if (expr->type != EXPR_DEREF) {
		if (expr->type == EXPR_PREOP && expr->op == '&')
			expr = strip_expr(expr->unop);
		else
			return -1;
	}

	if (expr->member_offset >= 0)
		return expr->member_offset;

	member = expr->member;
	if (!member)
		return -1;

	type = get_type(expr->deref);
	if (type_is_ptr(type))
		type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return -1;

	offset = get_member_offset(type, member->name);
	if (offset >= 0)
		expr->member_offset = offset;
	return offset;
}


