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

#include <fcntl.h>
#include <unistd.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

/*
 * Print a list of functions that return newly allocated memory.
 */

static struct tracker_list *allocated;

static void match_allocation(struct expression *expr,
			     const char *name, struct symbol *sym,
			     struct allocation_info *info)
{
	if (!sym)
		return;
	if (sym->ctype.modifiers & (MOD_NONLOCAL | MOD_STATIC | MOD_ADDRESSABLE))
		return;
	add_tracker(&allocated, my_id, name, sym);
}

static unsigned long returns_new_stuff;
static unsigned long returns_old_stuff;

static void match_return(struct expression *ret_value)
{
	char *name;
	struct symbol *sym;
	sval_t tmp;

	if (__inline_fn)
		return;
	if (get_value(ret_value, &tmp) && tmp.value == 0)
		return;
	returns_new_stuff = 1;
	name = expr_to_var_sym(ret_value, &sym);
	if (!name || !sym) {
		returns_old_stuff = 1;
		goto free;
	}
	if (!in_tracker_list(allocated, my_id, name, sym))
		returns_old_stuff = 1;
free:
	free_string(name);
}

static void match_end_func(struct symbol *sym)
{
	if (__inline_fn)
		return;
	if (returns_new_stuff && !returns_old_stuff)
		sm_info("allocation func");
	free_trackers_and_list(&allocated);
}

void check_allocation_funcs(int id)
{
	if (!option_info || option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_data(&returns_old_stuff);
	add_function_data(&returns_new_stuff);

	add_allocation_hook(&match_allocation);

	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, AFTER_FUNC_HOOK);
}
