/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"

static void clean_up_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		expand_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = NULL, **args;
	struct token *token;
	struct symbol_list *list;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	create_builtin_stream();

	args = argv;
	for (;;) {
		char *arg = *++args;
		if (!arg)
			break;
		if (arg[0] == '-') {
			args = handle_switch(arg + 1, args);
			continue;
		}
		filename = arg;
	}

	// Initialize type system
	init_ctype();

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", filename);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL, includepath);
	close(fd);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	list = translation_unit(token);

	// Simplification
	clean_up_symbols(list);

#if 1
	// Show the end result.
	show_symbol_list(list, "\n\n");
	printf("\n\n");
#endif

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
