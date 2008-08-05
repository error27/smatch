/*
 * Parse and linearize the tree for testing.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 * Licensed under the Open Software License version 1.1
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "linearize.h"

static void clean_up_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			show_entry(ep);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;

	clean_up_symbols(sparse_initialize(argc, argv, &filelist));
	FOR_EACH_PTR_NOTAG(filelist, file) {
		clean_up_symbols(sparse(file));
	} END_FOR_EACH_PTR_NOTAG(file);
	return 0;
}
