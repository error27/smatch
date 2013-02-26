/*
 * sparse/smatch_tracker.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
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
