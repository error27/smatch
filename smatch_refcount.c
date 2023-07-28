/*
 * Copyright (C) 2021 Oracle.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(inc);
STATE(dec);

static void match_inc(struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &inc);
}

static void match_dec(struct expression *expr, const char *name, struct symbol *sym)
{
	set_state(my_id, name, sym, &dec);
}

int was_inced(const char *name, struct symbol *sym)
{
	return get_state(my_id, name, sym) == &inc;
}

void register_refcount(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_modification_hook(my_id, &set_undefined);

	add_refcount_inc_hook(&match_inc);
	add_refcount_dec_hook(&match_dec);
}
