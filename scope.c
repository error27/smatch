/*
 * Symbol scoping.
 *
 * This is pretty trivial.
 */
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "symbol.h"
#include "scope.h"

static struct scope
	 base_scope = { .next = &base_scope },
	*current_scope = &base_scope;

void bind_scope(struct symbol *sym)
{
	add_symbol(&current_scope->symbols, sym);
}

void start_symbol_scope(void)
{
	struct scope *scope = __alloc_bytes(sizeof(*scope));
	memset(scope, 0, sizeof(*scope));
	scope->next = current_scope;
	current_scope = scope;
}

static void remove_symbol_scope(struct symbol *sym, void *data, int flags)
{
	struct symbol **ptr = sym->id_list;

	while (*ptr != sym)
		ptr = &(*ptr)->next_id;
	*ptr = sym->next_id;
}

void end_symbol_scope(void)
{
	struct scope *scope = current_scope;
	struct symbol_list *symbols = scope->symbols;

	current_scope = scope->next;
	scope->symbols = NULL;
	symbol_iterate(symbols, remove_symbol_scope, NULL);
}
