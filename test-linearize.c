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
	int fd;
	char *filename = NULL, **args;
	struct token *token;
	struct symbol_list *list;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	args = argv;
	for (;;) {
		char *arg = *++args;
		if (!arg)
			break;
		if (arg[0] == '-') {
			args = handle_switch(arg+1, args);
			continue;
		}
		filename = arg;
	}

	// Initialize type system
	init_ctype();

	create_builtin_stream();
	add_pre_buffer("#define __CHECKER__ 1\n");
	if (!preprocess_only)
		declare_builtin_functions();

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", filename);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL, includepath);
	close(fd);

	// Prepend any "include" file to the stream.
	if (include_fd >= 0)
		token = tokenize(include, include_fd, token, includepath);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	list = translation_unit(token);

	// Do type evaluation and simplify
	clean_up_symbols(list);
	return 0;
}
