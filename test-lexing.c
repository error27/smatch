#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include "token.h"

void die(const char *fmt, ...)
{
	va_list args;
	static char buffer[512];

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	fprintf(stderr, "%s\n", buffer);
	exit(1);
}

static char *show_value(struct value *value)
{
	static char buffer[256];

	switch (value->type) {
	case TOKEN_ERROR:
		return "syntax error";

	case TOKEN_IDENT: {
		struct ident *ident = value->ident;
		sprintf(buffer, "%.*s", ident->len, ident->name);
		return buffer;
	}

	case TOKEN_STRING: {
		char *ptr;
		int i;
		struct string *string = value->string;

		ptr = buffer;
		*ptr++ = '"';
		for (i = 0; i < string->length; i++) {
			unsigned char c = string->data[i];
			if (isprint(c) && c != '"') {
				*ptr++ = c;
				continue;
			}
			*ptr++ = '\\';
			switch (c) {
			case '\n':
				*ptr++ = 'n';
				continue;
			case '\t':
				*ptr++ = 't';
				continue;
			case '"':
				*ptr++ = '"';
				continue;
			}
			if (!isdigit(string->data[i+1])) {
				ptr += sprintf(ptr, "%o", c);
				continue;
			}
				
			ptr += sprintf(ptr, "%03o", c);
		}
		*ptr++ = '"';
		*ptr = '\0';
		return buffer;
	}

	case TOKEN_INTEGER: {
		char *ptr;
		ptr = buffer + sprintf(buffer, "%llu", value->intval);
		return buffer;
	}

	case TOKEN_FP: {
		sprintf(buffer, "%f", value->fpval);
		return buffer;
	}

	case TOKEN_SPECIAL: {
		int val = value->special;
		static const char *combinations[] = COMBINATION_STRINGS;
		buffer[0] = val;
		buffer[1] = 0;
		if (val >= SPECIAL_BASE)
			strcpy(buffer, combinations[val - SPECIAL_BASE]);
		return buffer;
	}
	
	default:
		return "WTF???";
	}
}

void callback(struct token *token)
{
	printf("%s ", show_value(&token->value));
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
	print_ident_stat();
	return 0;
}
