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
	token = tokenize(argv[1], fd);
	token = preprocess(token);
	translation_unit(token, &list);
	show_symbol_list(list);

	show_ident_alloc();
	show_token_alloc();
	show_symbol_alloc();
	show_expression_alloc();
	show_statement_alloc();
	show_string_alloc();
	show_bytes_alloc();
	return 0;
}
