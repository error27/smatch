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
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"

extern void emit_unit(const char *basename, struct symbol_list *list);

static void clean_up_symbol(struct symbol *sym, void *_parent, int flags)
{
	evaluate_symbol(sym);
	expand_symbol(sym);
}

int main(int argc, char **argv)
{
	int i, fd;
	char *basename, *filename = NULL, **args;
	struct token *token;

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

	if (filename == NULL)
		die("No file specified");

	basename = strrchr(filename, '/');
	if (!basename)
		basename = filename;
	else if ((basename == filename) && (basename[1] == 0)) {
		fprintf(stderr, "um\n");
		exit(1);
	} else {
		basename++;
		if (*basename == 0) {
			fprintf(stderr, "um\n");
			exit(1);
		}
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", argv[1]);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL);
	close(fd);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	translation_unit(token, &used_list);

	// Do type evaluation and simplification
	symbol_iterate(used_list, clean_up_symbol, NULL);

	// Show the end result.
	emit_unit(basename, used_list);

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
