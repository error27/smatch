/*
 * Helper routines
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

/*
 * Simple allocator for data that doesn't get partially free'd.
 * The tokenizer and parser allocate a _lot_ of small data structures
 * (often just two-three bytes for things like small integers),
 * and since they all depend on each other you can't free them
 * individually _anyway_. So do something that is very space-
 * efficient: allocate larger "blobs", and give out individual
 * small bits and pieces of it with no maintenance overhead.
 */
struct allocation_blob {
	struct allocation_blob *next;
	unsigned int left, offset;
	unsigned char data[];
};

struct allocator_struct {
	const char *name;
	struct allocation_blob *blobs;
	unsigned int alignment;
	unsigned int chunking;
	/* statistics */
	unsigned int allocations, total_bytes, useful_bytes;
};

void *allocate(struct allocator_struct *desc, unsigned int size)
{
	unsigned long alignment = desc->alignment;
	struct allocation_blob *blob = desc->blobs;
	void *retval;

	desc->allocations++;
	desc->useful_bytes += size;
	size = (size + alignment - 1) & ~(alignment-1);
	if (!blob || blob->left < size) {
		unsigned int offset, chunking = desc->chunking;
		struct allocation_blob *newblob = malloc(chunking);
		if (!newblob)
			die("out of memory");
		desc->total_bytes += chunking;
		newblob->next = blob;
		blob = newblob;
		desc->blobs = newblob;
		offset = offsetof(struct allocation_blob, data);
		if (alignment > offset)
			offset = alignment;
		blob->left = chunking - offset;
		blob->offset = offset - offsetof(struct allocation_blob, data);
	}
	retval = blob->data + blob->offset;
	blob->offset += size;
	blob->left -= size;
	return retval;
}

static void show_allocations(struct allocator_struct *x)
{
	fprintf(stderr, "%s: %d allocations, %d bytes (%d total bytes, "
			"%6.2f%% usage, %6.2f average size)\n",
		x->name, x->allocations, x->useful_bytes, x->total_bytes,
		100 * (double) x->useful_bytes / x->total_bytes,
		(double) x->useful_bytes / x->allocations);
}

struct allocator_struct ident_allocator = { "identifiers", NULL, __alignof__(struct ident), 8192 };
struct allocator_struct token_allocator = { "tokens", NULL, __alignof__(struct token), 8192 };
struct allocator_struct symbol_allocator = { "symbols", NULL, __alignof__(struct symbol), 8192 };
struct allocator_struct expression_allocator = { "expressions", NULL, __alignof__(struct expression), 8192 };
struct allocator_struct statement_allocator = { "statements", NULL, __alignof__(struct statement), 8192 };
struct allocator_struct string_allocator = { "strings", NULL, __alignof__(struct statement), 8192 };
struct allocator_struct bytes_allocator = { "bytes", NULL, 1, 8192 };

#define __ALLOCATOR(type, size, prepare, x)			\
	type *__alloc_##x(int extra)				\
	{							\
		type *ret = allocate(&x##_allocator, 		\
				size+extra);			\
		prepare;					\
		return ret;					\
	}							\
	void show_##x##_alloc(void)				\
	{							\
		show_allocations(&x##_allocator);		\
	}
#define ALLOCATOR(x) __ALLOCATOR(struct x, sizeof(struct x), memset(ret, 0, sizeof(struct x)), x)

ALLOCATOR(ident); ALLOCATOR(token); ALLOCATOR(symbol);
ALLOCATOR(expression); ALLOCATOR(statement); ALLOCATOR(string);
__ALLOCATOR(void, 0, , bytes);

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


