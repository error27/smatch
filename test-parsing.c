#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "token.h"
#include "parse.h"
#include "symbol.h"

int main(int argc, char **argv)
{
	int fd = open(argv[1], O_RDONLY);
	struct token *token;
	struct symbol *sym;

	if (fd < 0)
		die("No such file: %s", argv[1]);
	init_symbols();
	token = tokenize(argv[1], fd);

	token = translation_unit(token, &sym);
	if (token)
		warn(token, "Extra data");
	while (sym) {
		show_symbol(sym);
		printf("\n");
		sym = sym->next;
	}
	return 0;
}
