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

int main(int argc, char **argv)
{
	int fd = open(argv[1], O_RDONLY);
	struct token *token;
	struct symbol_list *list = NULL;

	if (fd < 0)
		die("No such file: %s", argv[1]);
	init_symbols();

	// Tokenize the input stream
	token = tokenize(argv[1], fd, NULL);
	close(fd);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	translation_unit(token, &list);

	// Show the end result.
	show_symbol_list(list, "\n\n");

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
