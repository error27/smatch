// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include "lib.h"
#include "symbol.h"

static void show_symbols(struct symbol_list *list, bool checker)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		printf("%s;\n", show_typename(sym, checker));
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;
	bool checker = true;

	if (argc > 1 && strcmp(argv[1], "--gcc") == 0) {
		checker = false;
		argv++;
		argc--;
	}

	sparse_initialize(argc, argv, &filelist);
	Wdecl = 0;
	FOR_EACH_PTR(filelist, file) {
		show_symbols(sparse(file), checker);
	} END_FOR_EACH_PTR(file);

	return has_error;
}
