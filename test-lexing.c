/*
 * Example test program that just uses the tokenization and
 * preprocessing phases, and prints out the results.
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

#include "token.h"
#include "symbol.h"

int main(int argc, char **argv)
{
	int fd = open(argv[1], O_RDONLY);
	struct token *token;

	if (fd < 0)
		die("No such file: %s", argv[1]);

	init_symbols();

	// Initialize type system
	init_ctype();

	token = tokenize(argv[1], fd, NULL);
	close(fd);
	token = preprocess(token);

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
	show_identifier_stats();
	return 0;
}
