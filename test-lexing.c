#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "token.h"

int main(int argc, char **argv)
{
	int fd = open(argv[1], O_RDONLY);
	struct token *token;

	if (fd < 0)
		die("No such file: %s", argv[1]);

	init_symbols();
	token = tokenize(argv[1], fd, NULL);
	close(fd);
	token = preprocess(token);

	while (!eof_token(token)) {
		struct token *next = token->next;
		char * separator = "";
		if (next->whitespace)
			separator = " ";
		if (next->newline)
			separator = "\n";
		printf("%s%s", show_token(token), separator);
		token = next;
	}
	putchar('\n');
	show_identifier_stats();
	return 0;
}
