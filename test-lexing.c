#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "token.h"

void callback(struct token *token)
{
	printf("%s ", show_token(token));
}

int main(int argc, char **argv)
{
	int line;
	int fd = open(argv[1], O_RDONLY);
	struct token *token;

	if (fd < 0)
		die("No such file: %s", argv[1]);

	token = tokenize(argv[1], fd);
	line = token->line;
	while (token) {
		callback(token);
		token = token->next;
		if (token && token->line != line) {
			line = token->line;
			putchar('\n');
		}
	}
	putchar('\n');
	show_identifier_stats();
	return 0;
}
