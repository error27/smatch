// SPDX-License-Identifier: MIT

#include <stdio.h>
#include "lib.h"
#include "symbol.h"

static void show_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		printf("%s;\n", show_typename(sym));
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;

	sparse_initialize(argc, argv, &filelist);
	Wdecl = 0;
	FOR_EACH_PTR(filelist, file) {
		show_symbols(sparse(file));
	} END_FOR_EACH_PTR(file);

	return has_error;
}
