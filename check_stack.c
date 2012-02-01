/*
 * smatch/check_stack.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The kernel has a small stack so putting huge structs and arrays on the
 * stack is a bug.
 *
 */

#include "smatch.h"

static int my_id;

static int total_size;
static int max_size;
static int max_lineno;
static int max_func_pos;
static int complained;

static void scope_end(int size)
{
	total_size -= size;
}

static void match_declarations(struct symbol *sym)
{
	struct symbol *base;
	const char *name;

	base = get_base_type(sym);
	if (sym->ctype.modifiers & MOD_STATIC)
		return;
	name = sym->ident->name;
	total_size += base->bit_size;
	if (total_size > max_size) {
		max_size = total_size;
		max_lineno = get_lineno();
		max_func_pos = get_func_pos();
	}
	if (base->bit_size >= 500 * 8) {
		complained = 1;
		sm_msg("warn: '%s' puts %d bytes on stack", name, base->bit_size / 8);
	}
	add_scope_hook((scope_hook *)&scope_end, INT_PTR(base->bit_size)); 
}

static void match_end_func(struct symbol *sym)
{
	if ((max_size >= 500 * 8) && !complained) {
		sm_printf("%s:%d %s(%d) ", get_filename(), max_lineno, get_function(), max_func_pos);
		sm_printf("warn: function puts %d bytes on stack\n", max_size / 8);
	}
	total_size = 0;
	complained = 0;
	max_size = 0;
	max_lineno = 0;
	max_func_pos = 0;
}

void check_stack(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
