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

bool buf_size_ok(struct expression *buf, struct expression *size)
{
	struct range_list *size_rl;
	int buf_size;

	if (!buf || !size)
		return false;

	buf_size = get_array_size_bytes_min(buf);
	if (buf_size > 0 &&
	    get_implied_rl(size, &size_rl) &&
	    !sval_is_negative(rl_min(size_rl)) &&
	    rl_max(size_rl).uvalue <= buf_size)
		return true;

	return buf_comp_has_bytes(buf, size);
}
