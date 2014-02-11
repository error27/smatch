/*
 * Copyright (C) 2009 Dan Carpenter.
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

static struct tracker_list *ignored;

void add_ignore(int owner, const char *name, struct symbol *sym)
{
	struct tracker *tmp;

	tmp = malloc(sizeof(*tmp));
	tmp->name = alloc_string(name);
	tmp->owner = owner;
	tmp->sym = sym;
	add_ptr_list(&ignored, tmp);
}

int is_ignored(int owner, const char *name, struct symbol *sym)
{
	struct tracker *tmp;

	FOR_EACH_PTR(ignored, tmp) {
		if (tmp->owner == owner && tmp->sym == sym
		    && !strcmp(tmp->name, name))
			return 1;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

static void clear_ignores(void)
{
	if (__inline_fn)
		return;
	__free_ptr_list((struct ptr_list **)&ignored);
}

void register_smatch_ignore(int id)
{
	add_hook(&clear_ignores, END_FUNC_HOOK);
}
