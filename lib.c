/*
 * Helper routines
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "token.h"

void warn(struct token *token, const char * fmt, ...)
{
	static char buffer[512];
	const char *name;
	int pos,line;

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	name = "EOF";
	pos = 0;
	line = 0;
	if (token) {
		name = input_streams[token->stream].name;
		pos = token->pos;
		line = token->line;
	}
		
	fprintf(stderr, "warning: %s:%d:%d: %s\n",
		name, line, pos, buffer);
}	


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


