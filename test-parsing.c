/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
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

char *includepath[] = {
	"/usr/lib/gcc-lib/i386-redhat-linux/3.2.1/include/",
#if 1
	"/home/torvalds/v2.5/linux/include/",
	"/home/torvalds/v2.5/linux/include/asm-i386/mach-default/",
#else
	"/usr/include/",
	"/usr/local/include/",
#endif
	"",
	NULL
};

static void handle_switch(char *arg)
{
}

int main(int argc, char **argv)
{
	int i, fd;
	char *filename = NULL;
	struct token *token;
	struct symbol_list *list = NULL;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (arg[0] == '-') {
			handle_switch(arg+1);
			continue;
		}
		filename = arg;
	}
		

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", argv[1]);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL);
	close(fd);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	translation_unit(token, &list);

	// Show the end result.
	show_symbol_list(list, "\n\n");
	printf("\n\n");

	// And show the allocation statistics
	show_ident_alloc();
	show_token_alloc();
	show_symbol_alloc();
	show_expression_alloc();
	show_statement_alloc();
	show_string_alloc();
	show_bytes_alloc();
	return 0;
}
