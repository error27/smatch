/*
 * Copyright (C) 2026 Dan Carpenter.
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

void expr_set_parent_expr(struct expression *expr, struct expression *parent)
{
	struct expression *prev;

	if (!expr || !parent)
		return;

	prev = expr_get_parent_expr(expr);
	if (prev == parent)
		return;

	if (parent && parent->smatch_flags & Tmp)
		return;

	expr->parent = (unsigned long)parent | 0x1UL;
}

void expr_set_parent_stmt(struct expression *expr, struct statement *parent)
{
	if (!expr)
		return;
	expr->parent = (unsigned long)parent;
}

struct expression *expr_get_parent_expr(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (!(expr->parent & 0x1UL))
		return NULL;

	parent = (struct expression *)(expr->parent & ~0x1UL);
	if (parent && (parent->smatch_flags & Fake))
		return expr_get_parent_expr(parent);
	return parent;
}

struct expression *expr_get_fake_parent_expr(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (!(expr->parent & 0x1UL))
		return NULL;

	parent = (struct expression *)(expr->parent & ~0x1UL);
	if (parent && (parent->smatch_flags & Fake))
		return parent;
	return NULL;
}

struct expression *expr_get_fake_or_real_parent_expr(struct expression *expr)
{
	struct expression *parent;

	parent = expr_get_fake_parent_expr(expr);
	if (parent)
		return parent;
	return expr_get_parent_expr(expr);

}

struct statement *expr_get_parent_stmt(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (expr->parent & 0x1UL) {
		parent = (struct expression *)(expr->parent & ~0x1UL);
		if (parent->smatch_flags & Fake)
			return expr_get_parent_stmt(parent);
		return NULL;
	}
	return (struct statement *)expr->parent;
}

struct statement *get_parent_stmt(struct expression *expr)
{
	struct expression *tmp;
	int count = 20;

	if (!expr)
		return NULL;
	while (--count > 0 && (tmp = expr_get_parent_expr(expr)))
		expr = tmp;
	if (!count)
		return NULL;

	return expr_get_parent_stmt(expr);
}
