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

static void clean_up_symbol(struct symbol *sym, void *_parent, int flags)
{
	check_duplicates(sym);
	evaluate_symbol(sym);
	expand_symbol(sym);
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = NULL, **args;
	struct token *token;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	create_builtin_stream();
	add_pre_buffer("extern void *__builtin_memcpy(void *, const void *, unsigned long);\n");
	add_pre_buffer("extern void * __builtin_return_address(int);\n");

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
		

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", filename);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL);
	close(fd);

	// Prepend any "include" file to the stream.
	if (include_fd >= 0)
		token = tokenize(include, include_fd, token);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	if (preprocess_only) {
		while (!eof_token(token)) {
			int prec = 1;
			struct token *next = token->next;
			char * separator = "";
			if (next->pos.whitespace)
				separator = " ";
			if (next->pos.newline) {
				separator = "\n\t\t\t\t\t";
				prec = next->pos.pos;
				if (prec > 4)
					prec = 4;
			}
			printf("%s%.*s", show_token(token), prec, separator);
			token = next;
		}
		putchar('\n');

		return 0;
	} 

	// Parse the resulting C code
	translation_unit(token, &used_list);

	// Do type evaluation and simplify
	symbol_iterate(used_list, clean_up_symbol, NULL);
	return 0;
}
