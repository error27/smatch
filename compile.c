/*
 * Example trivial client program that uses the sparse library
 * and x86 backend.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 * Copyright 2003 Jeff Garzik
 *
 *  Licensed under the Open Software License version 1.1
 *
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
#include "compile.h"

static void clean_up_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		expand_symbol(sym);
		emit_one_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	const char *filename;

	clean_up_symbols(sparse_initialize(argc, argv));
	while ((filename = *argv) != NULL) {
		struct symbol_list *list;
		const char *basename = strrchr(filename, '/');
		if (basename)
			filename = basename+1;
		list = sparse(argv);

		// Do type evaluation and simplification
		emit_unit_begin(filename);
		clean_up_symbols(list);
		emit_unit_end();
	}

#if 0
	// And show the allocation statistics
	show_ident_alloc();
	show_token_alloc();
	show_symbol_alloc();
	show_expression_alloc();
	show_statement_alloc();
	show_string_alloc();
	show_bytes_alloc();
#endif
	return 0;
}
