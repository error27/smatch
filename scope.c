/*
 * Symbol scoping.
 *
 * This is pretty trivial.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lib.h"
#include "allocate.h"
#include "symbol.h"
#include "scope.h"

static struct scope builtin_scope = { .next = &builtin_scope };

struct scope	*block_scope = &builtin_scope,		// regular automatic variables etc
		*function_scope = &builtin_scope,	// labels, arguments etc
		*file_scope = &builtin_scope,		// static
		*global_scope = &builtin_scope;		// externally visible

void bind_scope(struct symbol *sym, struct scope *scope)
{
	sym->scope = scope;
	add_symbol(&scope->symbols, sym);
}

static void start_scope(struct scope **s)
{
	struct scope *scope = __alloc_scope(0);
	memset(scope, 0, sizeof(*scope));
	scope->next = *s;
	*s = scope;
}

void start_file_scope(void)
{
	struct scope *scope = __alloc_scope(0);

	memset(scope, 0, sizeof(*scope));
	scope->next = &builtin_scope;
	file_scope = scope;

	/* top-level stuff defaults to file scope, "extern" etc will choose global scope */
	function_scope = scope;
	block_scope = scope;
}

void start_symbol_scope(void)
{
	start_scope(&block_scope);
}

void start_function_scope(void)
{
	start_scope(&function_scope);
	start_scope(&block_scope);
}

static void remove_symbol_scope(struct symbol *sym)
{
	struct symbol **ptr = sym->id_list;

	while (*ptr != sym)
		ptr = &(*ptr)->next_id;
	*ptr = sym->next_id;
}

static void end_scope(struct scope **s)
{
	struct scope *scope = *s;
	struct symbol_list *symbols = scope->symbols;
	struct symbol *sym;

	*s = scope->next;
	scope->symbols = NULL;
	FOR_EACH_PTR(symbols, sym) {
		remove_symbol_scope(sym);
	} END_FOR_EACH_PTR(sym);
}

void end_file_scope(void)
{
	end_scope(&file_scope);
}

void new_file_scope(void)
{
	if (file_scope != &builtin_scope)
		end_file_scope();
	start_file_scope();
}

void end_symbol_scope(void)
{
	end_scope(&block_scope);
}

void end_function_scope(void)
{
	end_scope(&block_scope);
	end_scope(&function_scope);
}
