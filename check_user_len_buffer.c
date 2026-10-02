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

static void check_buffer(struct expression *buffer, struct expression *size_expr,
			 sval_t size)
{
	struct range_list *rl;
	char *name;

	if (buf_size_ok(buffer, size_expr))
		return;
	if (!buffer || !get_user_len(buffer, &rl))
		return;
	if (sval_cmp(rl_min(rl), size) >= 0)
		return;

	name = expr_to_str(buffer);
	sm_warning("buffer '%s' too small user_len=%s for %lld byte copy",
		   name, show_rl(rl), size.value);
	free_string(name);
}

static void match_memcpy(struct expression *dest, struct expression *src,
			 struct expression *size_expr, sval_t size)
{
	check_buffer(dest, size_expr, size);
	check_buffer(src, size_expr, size);
}

static void match_copy(struct expression *dest, struct expression *src,
			struct expression *size)
{
	sval_t sval;

	if (!get_implied_value(size, &sval))
		return;

	match_memcpy(dest, src, size, sval);
}

void check_user_len_buffer(int id)
{
	add_memcpy_hook(&match_copy);
}
