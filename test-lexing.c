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

	token = tokenize(argv[1], fd, NULL);
	token = preprocess(token);
	while (!eof_token(token)) {
		struct token *next = token->next;
		char separator = ' ';
		if (next->newline)
			separator = '\n';
		printf("%s%c", show_token(token), separator);
		token = next;
	}
	putchar('\n');
	show_identifier_stats();
	return 0;
}
