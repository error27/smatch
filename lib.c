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
	struct stream *stream;

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	stream = input_streams + token->stream;
	fprintf(stderr, "warning: %s:%d: %s\n",
		stream->name, token->line,
		buffer);
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


