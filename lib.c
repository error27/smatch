/*
 * Helper routines
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

#define ALLOCATOR(x)						\
	unsigned int __size_##x;				\
	unsigned int __nr_##x;					\
	struct x *__alloc_##x(int extra)			\
	{							\
		struct x *ret = malloc(sizeof(*ret)+extra);	\
		if (!ret) die("unable to allocate " #x);	\
		__size_##x += sizeof(*ret)+extra;		\
		__nr_##x ++;					\
		memset(ret, 0, sizeof(*ret));			\
		return ret;					\
	}							\
	void show_##x##_alloc(void)				\
	{							\
		fprintf(stderr, #x ": %d allocations, "		\
			"%d bytes\n", __nr_##x, __size_##x);	\
	}

ALLOCATOR(ident); ALLOCATOR(token); ALLOCATOR(symbol);
ALLOCATOR(expression); ALLOCATOR(statement);

void iterate(struct ptr_list *list, void (*callback)(void *))
{
	while (list) {
		int i;
		for (i = 0; i < list->nr; i++)
			callback(list->list[i]);
		list = list->next;
	}
}

void add_ptr_list(struct ptr_list **listp, void *ptr)
{
	struct ptr_list *list = *listp;
	int nr;

	if (!list || (nr = list->nr) >= LIST_NODE_NR) {
		struct ptr_list *newlist = malloc(sizeof(*newlist));
		if (!newlist)
			die("out of memory for symbol/statement lists");
		memset(newlist, 0, sizeof(*newlist));
		newlist->next = list;
		list = newlist;
		*listp = newlist;
		nr = 0;
	}
	list->list[nr] = ptr;
	nr++;
	list->nr = nr;
}

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


