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
	struct statement *stmt;

	if (fd < 0)
		die("No such file: %s", argv[1]);
	init_symbols();
	token = tokenize(argv[1], fd);

	token = statement_list(token, &stmt);
	if (token)
		warn(token, "Extra data");
	while (stmt) {
		show_expression(stmt->expression);
		printf("\n");
		stmt = stmt->next;
	}
	return 0;
}
